// The replay recorder (see replay.h): the session decides when files open and close and when a
// sample is due; this file feeds it from the game.
// - replay_onEvent (the readEvent hook): the event's type, the session's join/connection rules, then
//   the event's record from its payload (ev+12).
// - replay_onInputPoll (the mouse hook, many times a second on the game thread): the 30 s silent end,
//   the 1 s flush, and on each due sample the players, then the soldiers and vehicles with their armour,
//   parts, engines, states and radios, the control points, the tickets and the player's hit mark
//   (stages 2-3, plan-1b; Session D's offsets); replay-recorder-2 adds the info labels, the rounds in
//   flight (pn/pj/pd, Projectile vtable 0x00B92294) and the far soldiers.
// - The event hook takes a due sample too, for the spawn screen, where the game stops polling the
//   mouse but the server's events keep coming.
// - replay_onFire (fire_hook.cpp, every round fireBarrel fires) queues the round; both hooks above write
//   the queued rounds first, each at its own time, so the file stays in time order.
// - replay_shutdown (DllMain at process exit): the end line and a last flush.
// All of them return at once while [replay] recordReplays is off.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cctype>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "bfv_addresses.h"
#include "plus_log.h"
#include "replay.h"
#include "replay_events.h"
#include "replay_read.h"
#include "replay_session.h"
#include "replay_settings.h"
#include "replay_track.h"
#include "seh_read.h"

namespace {

const char* const kVersion = "0.3-dev";
const size_t kPayloadBytes = 128;   // the longest event the decoder reads (setLevel: two char[64])

std::mutex g_mu;   // the game's events and polls share one thread (Session A); detach comes from another
std::unique_ptr<ReplaySession> g_session;
std::unique_ptr<EventDecoder> g_events;
ObjectTracker g_objects;
PlayerTracker g_players;
// Stages 2-3 (plan-1b Tasks 9-10).
TicketTracker g_tickets;
CpTracker g_cps;
PartTracker g_parts;
SoldierTracker g_soldiers;
HitMarkTracker g_hits;
MusicTracker g_music;
ShotQueue g_shots;
// replay-recorder-2: the run's labels, rounds in flight, soldiers past the client's animation range.
InfoTracker g_info;
ProjectileTracker g_proj;
FarTracker g_far;
std::atomic<bool> g_live{ false };          // a file is open: the fire hook queues rounds
std::atomic<uintptr_t> g_localBF{ 0 };      // the recording player's BFPlayer, for a shot's `local`
std::string g_path;
bool g_loggedOpenFail = false, g_loggedRead = false, g_loggedListFail = false, g_loggedNoObjects = false;
bool g_loggedDetail = false, g_loggedDrops = false;
int g_closesLogged = 0;
unsigned g_samplesWithoutObjects = 0;

uint64_t nowMs()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 1000 / freq.QuadPart);
}

// The mod folder from `+game <mod>` on the command line; BFV's own when absent.
std::string modFromCommandLine()
{
    std::string cmd = GetCommandLineA();
    std::string lower = cmd;
    for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
    size_t at = lower.find("+game ");
    if (at == std::string::npos) return "bfvietnam";
    at += 6;
    while (at < lower.size() && lower[at] == ' ') ++at;
    size_t end = lower.find(' ', at);
    std::string mod = lower.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (!mod.empty() && mod.front() == '"') mod = mod.substr(1);
    if (!mod.empty() && mod.back() == '"') mod.pop_back();
    return mod.empty() ? "bfvietnam" : mod;
}

void ensureSession()
{
    if (g_session) return;
    SessionConfig cfg{ g_replay.record, g_replay.sampleHz, plusExeDir() + "replays", kVersion, modFromCommandLine(),
        __DATE__ " " __TIME__ };
    g_session.reset(new ReplaySession(cfg));
    g_events.reset(new EventDecoder(templateNameById));
}

// A new file: fresh trackers and repeat rules, so its first sample writes every o and the roster.
void noticeNewFile()
{
    if (!g_session->recording() || g_session->path() == g_path) return;
    g_path = g_session->path();
    g_objects.reset();
    g_players.reset();
    g_tickets.reset();
    g_cps.reset();
    g_parts.reset();
    g_soldiers.reset();
    g_hits.reset();
    g_music.reset();
    g_shots.clear();
    g_info.reset();
    g_proj.reset();
    g_far.reset();
    g_localBF = 0;
    g_events.reset(new EventDecoder(templateNameById));
    templateCacheReset();
    g_loggedRead = g_loggedListFail = g_loggedNoObjects = g_loggedDetail = g_loggedDrops = false;
    g_samplesWithoutObjects = 0;
    plusLog("replay: recording to %s", g_path.c_str());
}

// Each file that closed since the last look, with why (no disconnect signal exists, so the status
// log is how a lab session checks the close rules).
void logCloses()
{
    if (g_session->closeCount() == g_closesLogged) return;
    g_closesLogged = g_session->closeCount();
    plusLog("replay: closed %s (%s), end t=%.3f", g_path.c_str(), g_session->lastClose().c_str(), g_session->lastActivityT());
}

// The rounds fired since the last hook, each at its own time, before the hook writes anything else (so
// the file stays in time order). Called with g_mu held.
void drainShots()
{
    static std::vector<QueuedShot> q;
    g_shots.drain(q);
    if (q.empty() || !g_session || !g_session->recording()) return;
    std::string line;
    for (const QueuedShot& s : q)
        if (shotLine(g_session->t(s.ms), s.shot, line)) g_session->write(line);
}

}  // namespace

static void sampleIfDue(uint64_t now);

// fire_hook.cpp, once per round (game thread, without g_mu: it only reads the game and queues).
void replay_onFire(uintptr_t fireArms, uintptr_t player, uintptr_t mat)
{
    if (!g_live.load(std::memory_order_relaxed)) return;
    LiveShot s;
    uintptr_t bf = 0;
    if (!readShot(fireArms, player, mat, s, bf)) return;
    s.local = bf && bf == g_localBF.load(std::memory_order_relaxed);
    g_shots.push(nowMs(), s);
}

void replay_onEvent(void* ev)
{
    if (!g_replay.record) return;
    std::lock_guard<std::mutex> lock(g_mu);
    ensureSession();
    uintptr_t type = 0;
    if (!seh::vcall0((uintptr_t)ev, 0, &type) || type > 0xff) return;
    const uint64_t now = nowMs();
    std::time_t tt = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &tt);
    drainShots();   // into the file they were fired in, before a new join closes it
    g_session->onEvent(now, local, isServerInfo((int)type));
    logCloses();
    if (type == 0x36) {
        // SetLevelEvent: a new level's templates, and control points and radios that may sit at the old
        // ones' ids and addresses. Parts tell a reused address by its root and template already.
        templateCacheReset();
        g_cps.reset();
        g_music.reset();
    }
    if (!g_session->recording()) {
        g_live = false;
        if (!g_loggedOpenFail) { g_loggedOpenFail = true; plusLog("replay: cannot write %s", g_session->path().c_str()); }
        return;
    }
    g_loggedOpenFail = false;
    noticeNewFile();
    g_live = true;
    drainShots();
    uint8_t payload[kPayloadBytes];
    size_t n = kPayloadBytes;
    if (!seh::read((uintptr_t)ev + bfv::EVENT_PAYLOAD, payload, n)) {
        n = 32;   // near the end of a heap block: the short events still decode
        if (!seh::read((uintptr_t)ev + bfv::EVENT_PAYLOAD, payload, n)) return;
    }
    std::string line;
    if (g_events->line((int)type, payload, n, g_session->t(now), line)) g_session->write(line);
    g_info.onEvent((int)type, payload, n);   // serverName, setLevel: the info line's labels
    // The game stops polling the mouse on the spawn screen (Session C: 23 s with no samples while
    // bots moved), but the server's events keep coming on the same thread: take a due sample here too.
    sampleIfDue(now);
}

void replay_onInputPoll()
{
    if (!g_replay.record) return;
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_session) return;   // nothing before the first event of a connection
    sampleIfDue(nowMs());
}

// Called with g_mu held, from either hook (both run on the game thread).
static void sampleIfDue(uint64_t now)
{
    const bool due = g_session->onFrame(now, localPlayerPresent());
    logCloses();
    g_live = g_session->recording();
    drainShots();
    if (!due || !g_session->recording()) return;
    const double t = g_session->t(now);
    std::vector<std::string> lines;
    std::vector<LivePlayer> players;
    readPlayers(players);
    g_players.sample(t, players, lines);
    const LivePlayer* me = nullptr;
    for (const auto& p : players)
        if (p.local) me = &p;
    g_info.sample(t, me ? me->name : std::string(), lines);
    std::vector<LiveObject> objects;
    ReadStats st;
    DetailRead detail;
    const int n = readObjects(objects, &st, &detail);
    AnimTable table{};
    bool tableRead = false;
    if (n >= 0) {
        g_objects.sample(t, objects, lines);   // d, o, s, a
        g_parts.sample(t, detail.parts, detail.engines, lines);   // jn, j, g
        if (!detail.soldiers.empty() && g_soldiers.needsAnimTable()) tableRead = readAnimTable(table);
        g_soldiers.sample(t, detail.soldiers, tableRead ? &table : nullptr, lines);   // anim once, st
        g_cps.sample(t, detail.cps, lines);
        g_music.sample(t, detail.music, lines);
        g_proj.sample(t, detail.projectiles, lines);   // pn, pj, pd
        // far: each soldier's distance from the player's own soldier or vehicle (none while he is dead)
        std::unordered_map<int, const LiveObject*> byNet;
        for (const auto& o : objects) byNet[o.netId] = &o;
        const float* view = nullptr;
        if (me) {
            auto it = byNet.find(me->rootNetId);
            if (it != byNet.end()) view = &it->second->rows[12];
        }
        std::vector<SoldierPos> soldierPos;
        for (const auto& s : detail.soldiers) {
            auto it = byNet.find(s.netId);
            if (it != byNet.end()) soldierPos.push_back({ s.netId, it->second->rows[12], it->second->rows[13], it->second->rows[14] });
        }
        g_far.sample(t, view, soldierPos, lines);
    } else if (!g_loggedListFail) {
        g_loggedListFail = true;
        plusLog("replay: the object list can't be read; recording events and players only");
    }
    LiveTickets tickets{};
    readTickets(tickets);
    g_tickets.sample(t, tickets, lines);
    float mark = 0;
    int pid = -1;
    uintptr_t bf = 0;
    const bool haveMark = readHitMark(mark, pid, bf);
    g_localBF = bf;
    g_hits.sample(t, pid, haveMark, mark, lines);
    for (const auto& l : lines) g_session->write(l);
    if (!g_loggedDrops && g_shots.dropped()) {
        g_loggedDrops = true;
        plusLog("replay: rounds fired faster than they could be written; %u left out so far in this file", (unsigned)g_shots.dropped());
    }
    // Once a file, when soldiers are in the sample: what stages 2-3 found, to check them in the lab.
    if (!g_loggedDetail && n >= 0 && !detail.soldiers.empty()) {
        g_loggedDetail = true;
        unsigned armoured = 0;
        for (const auto& o : objects) armoured += o.hasArmor;
        plusLog("replay: detail: armour %u of %u, soldiers %u, parts %u, engines %u, radios %u, control points %u; tickets %s; "
                "animation table %s (%u states)", armoured, (unsigned)objects.size(), (unsigned)detail.soldiers.size(),
            (unsigned)detail.parts.size(), (unsigned)detail.engines.size(), (unsigned)detail.music.size(),
            (unsigned)detail.cps.size(), tickets.valid ? "read" : "not found", tableRead ? "read" : "not read",
            (unsigned)table.total);
    }
    // Once a file, when in game: what the walk found, to check the offsets against the real game.
    if (!g_loggedRead && n >= 0 && !players.empty() && st.soldiers + st.vehicles > 0) {
        g_loggedRead = true;
        plusLog("replay: object list %u, walked %u, soldiers %u, vehicles %u; players %u",
            st.listSize, st.walked, st.soldiers, st.vehicles, (unsigned)players.size());
    }
    // In game but no soldier or vehicle found for ~3 s at 30 Hz: the object reads don't fit this exe.
    bool inGame = false;
    for (const auto& p : players) inGame = inGame || p.local;
    if (inGame && n >= 0 && st.soldiers + st.vehicles == 0 && ++g_samplesWithoutObjects == 90 && !g_loggedNoObjects) {
        g_loggedNoObjects = true;
        plusLog("replay: players but no soldiers or vehicles found (object list %u, walked %u); the object reads may not match this exe",
            st.listSize, st.walked);
    }
}

void replay_shutdown()
{
    g_live = false;
    // At process exit the game's threads are already gone; one may have died holding the lock, so
    // never wait for it (the file then just keeps what its last flush wrote).
    if (!g_mu.try_lock()) {
        // Don't let the static destructors write it either: the buffer may be mid-append.
        (void)g_session.release();
        (void)g_events.release();
        return;
    }
    if (g_session) {
        drainShots();
        g_session->close();
    }
    g_mu.unlock();
}
