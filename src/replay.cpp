// The replay recorder (see replay.h): the session decides when files open and close and when a
// sample is due; this file feeds it from the game.
// - replay_onEvent (the readEvent hook): the event's type, the session's join/connection rules, then
//   the event's record from its payload (ev+12).
// - replay_onInputPoll (the mouse hook, ~28 a second on the game thread): the 30 s silent end, the
//   2 s flush, and on each due sample the players, then the soldiers and vehicles.
// - replay_shutdown (DllMain at process exit): the end line and a last flush.
// All three return at once while [replay] recordReplays is off.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cctype>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
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
std::string g_path;
bool g_loggedOpenFail = false, g_loggedRead = false, g_loggedListFail = false;

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
    g_events.reset(new EventDecoder(templateNameById));
    g_loggedRead = g_loggedListFail = false;
    plusLog("replay: recording to %s", g_path.c_str());
}

}  // namespace

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
    g_session->onEvent(now, local, isServerInfo((int)type));
    if (!g_session->recording()) {
        if (!g_loggedOpenFail) { g_loggedOpenFail = true; plusLog("replay: cannot write %s", g_session->path().c_str()); }
        return;
    }
    g_loggedOpenFail = false;
    noticeNewFile();
    uint8_t payload[kPayloadBytes];
    size_t n = kPayloadBytes;
    if (!seh::read((uintptr_t)ev + bfv::EVENT_PAYLOAD, payload, n)) {
        n = 32;   // near the end of a heap block: the short events still decode
        if (!seh::read((uintptr_t)ev + bfv::EVENT_PAYLOAD, payload, n)) return;
    }
    std::string line;
    if (g_events->line((int)type, payload, n, g_session->t(now), line)) g_session->write(line);
}

void replay_onInputPoll()
{
    if (!g_replay.record) return;
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_session) return;   // nothing before the first event of a connection
    const uint64_t now = nowMs();
    const bool due = g_session->onFrame(now, localPlayerPresent());
    if (!due || !g_session->recording()) return;
    const double t = g_session->t(now);
    std::vector<std::string> lines;
    std::vector<LivePlayer> players;
    readPlayers(players);
    g_players.sample(t, players, lines);
    std::vector<LiveObject> objects;
    ReadStats st;
    const int n = readObjects(objects, &st);
    if (n >= 0) g_objects.sample(t, objects, lines);
    else if (!g_loggedListFail) { g_loggedListFail = true; plusLog("replay: the object list can't be read; recording events and players only"); }
    for (const auto& l : lines) g_session->write(l);
    // Once a file, when in game: what the walk found, to check the offsets against the real game.
    if (!g_loggedRead && n >= 0 && !players.empty() && st.soldiers + st.vehicles > 0) {
        g_loggedRead = true;
        plusLog("replay: object list %u, walked %u, soldiers %u, vehicles %u; players %u",
            st.listSize, st.walked, st.soldiers, st.vehicles, (unsigned)players.size());
    }
}

void replay_shutdown()
{
    // At process exit the game's threads are already gone; one may have died holding the lock, so
    // never wait for it (the file then just keeps what its last flush wrote).
    if (!g_mu.try_lock()) return;
    if (g_session) g_session->close();
    g_mu.unlock();
}
