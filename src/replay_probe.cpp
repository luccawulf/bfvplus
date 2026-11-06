// The address-finding probe, v2 (see replay_probe.h). Everything it writes is raw: the matching against
// the lab server recorder's file happens offline (sd_match.py in the viewer repo, plan-1b Task 6).
//
// Lines in logs\bfvplus_probe.log (JSON, one per line; t = seconds since the probe started; addresses
// and vtables as "0x%08X"; raw bytes as hex):
//   hello    once: the probe version and the static offsets it assumes
//   thread   every 10 s per callback kind: the thread ids seen and calls per second
//   stack    once: the return addresses above the readEvent hook
//   ev       the first 20 events of each type: 96 bytes from the event pointer
//   local    every 5 s: the player manager, the local player and the object he controls
//   mgr      every 5 s: the object manager's first 0x140 bytes
//   pl       every 5 s: each player in the player list, 0x300 bytes
//   sol      every 5 s: each root soldier with a net id, 0x600 bytes
//   veh      every 5 s: each root vehicle with a net id, its template, 0x200 bytes
//   comp     every 5 s: each such root's component map, [key, value, vtable, bytes] (0x60 bytes; 0x130
//            for an Armor)
//   cp       every 5 s: each ControlPoint: gid, net id, template, 0x300 bytes, its templates' 0x380 bytes
//            and the MSVC strings in them
//   score    every 5 s: *(0x00D65748), its vtable and 0x200 bytes, and the exe dwords pointing at it;
//            a heap scan for the ScoreManager vtable (at most every 10 s, at most 12 times) only while
//            the global holds no ScoreManager
//   fa       every 5 s: FireArms and HandFireArms under roots within 150 m of the local player (at most
//            96): template, root net id, parent chain, 0x380 bytes (opaque: nothing here is decoded)
//   eng      every 5 s: Engines under roots within 300 m (at most 96): 0x200 bytes, the PhysicsEngine
//            pointer, vtable and 0x100 bytes
//   part     every 5 s: each child of a root vehicle within 200 m (at most 400): vtable, template, net
//            id, parent chain, flags, 64 bytes at *(+0x8C) (relative) and 64 at +0x94 (cached absolute)
//   mus      every 5 s: each MusicPlayer: root net id, 0x180 bytes
//   anim     once, when readable: the state table, per state [index, flags, [[offset, "text"], ...]]
//   lp       each GetDeviceState poll: the dwords of the local player's 0x300 bytes that changed
//   fb       each fireBarrel call (the fire hook): weapon, root, player, barrel index, the Mat4, and the
//            weapon's 0x380 bytes for its first three calls
//   dumpEnd  every 5 s: the dump's milliseconds and counts
//
// All of it comes from the object list walk (stage 1's hash_map list) and the player list: no heap
// scan apart from the ScoreManager fallback. Reads are SEH-guarded through the small helpers below
// (functions with __try hold no C++ objects). The fire hook observes only: it calls fireBarrel with the
// arguments it was given.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "bfv_addresses.h"
#include "plus_log.h"
#include "replay_probe.h"

namespace {

const uintptr_t VT_PCO = VT_PlayerControlObject.va;
const uintptr_t VT_SOLDIER = VT_BFSoldier.va;
const uintptr_t VT_PLAYER = VT_BFPlayer.va, VT_PLAYER_4 = 0x00B5F6F4, VT_PLAYER_8 = 0x00B5F6C4;
const size_t SOL_BYTES = 0x600, VEH_BYTES = 0x200, PLAYER_BYTES = 0x300, COMP_BYTES = 0x60, ARMOR_BYTES = 0x130;
const size_t CP_BYTES = 0x300, TMPL_BYTES = 0x380, SCORE_BYTES = 0x200, FA_BYTES = 0x380, ENG_BYTES = 0x200;
const size_t PE_BYTES = 0x100, MUS_BYTES = 0x180, STATE_SCAN = 0x200;
const int MAX_FA = 96, MAX_ENG = 96, MAX_PART = 400, MAX_COMP = 64;
const float FA_RANGE = 150.0f, ENG_RANGE = 300.0f, PART_RANGE = 200.0f;

std::mutex g_outMu;
FILE* g_out = nullptr;
bool g_outFailed = false;
LARGE_INTEGER g_qpf, g_qp0;
bool g_clockInit = false;

double nowS()
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    if (!g_clockInit) { QueryPerformanceFrequency(&g_qpf); g_qp0 = q; g_clockInit = true; }
    return (double)(q.QuadPart - g_qp0.QuadPart) / (double)g_qpf.QuadPart;
}

void out(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_outMu);
    if (g_outFailed) return;
    if (!g_out) {
        std::string dir = plusExeDir() + "logs";
        CreateDirectoryA(dir.c_str(), nullptr);
        g_out = std::fopen((dir + "\\bfvplus_probe.log").c_str(), "w");
        if (!g_out) { g_outFailed = true; plusLog("probe: cannot open logs\\bfvplus_probe.log"); return; }
        plusLog("probe: writing logs\\bfvplus_probe.log (v2)");
    }
    std::fputs(line.c_str(), g_out);
    std::fputc('\n', g_out);
}

void outFlush()
{
    std::lock_guard<std::mutex> lock(g_outMu);
    if (g_out) std::fflush(g_out);
}

bool ptrSane(uintptr_t p) { return p >= 0x00010000 && p < 0x7FF00000; }
bool inImage(uintptr_t p) { return p >= 0x00400000 && p < 0x01400000; }

// ---- SEH helpers: no C++ objects in these ------------------------------------------------
bool safeRead(uintptr_t p, void* dst, size_t n)
{
    if (!ptrSane(p)) return false;
    __try { std::memcpy(dst, (const void*)p, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t u32(uintptr_t p)
{
    uintptr_t v = 0;
    return safeRead(p, &v, 4) ? v : 0;
}

bool safeVcall0(uintptr_t obj, unsigned slot, uintptr_t* result)
{
    if (!ptrSane(obj)) return false;
    __try {
        uintptr_t vt = *(const uintptr_t*)obj;
        if (!inImage(vt)) return false;
        uintptr_t fn = *(const uintptr_t*)(vt + 4 * slot);
        if (!inImage(fn)) return false;
        *result = (uintptr_t)((void*(__thiscall*)(void*))fn)((void*)obj);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int scanRegion(const uint8_t* base, size_t size, uintptr_t want, uintptr_t* hits, int n, int cap)
{
    __try {
        const uint8_t* end = base + size - 4;
        for (const uint8_t* p = base; p <= end && n < cap; p += 4)
            if (*(const uintptr_t*)p == want) hits[n++] = (uintptr_t)p;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

int scanGlobals(uintptr_t lo, uintptr_t hi, uintptr_t want, uintptr_t* found, int n, int cap)
{
    __try {
        for (uintptr_t p = lo; p + 4 <= hi && n < cap; p += 4)
            if (*(const uintptr_t*)p == want) found[n++] = p;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// ---- text helpers ------------------------------------------------------------------------
std::string hx(uintptr_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "\"0x%08X\"", (unsigned)v);
    return b;
}

std::string hexOf(const uint8_t* p, size_t n)
{
    std::string s;
    s.reserve(n * 2 + 2);
    s += '"';
    char b[4];
    for (size_t i = 0; i < n; ++i) { std::snprintf(b, sizeof(b), "%02x", p[i]); s += b; }
    return s + '"';
}

std::string hexBytes(uintptr_t p, size_t n)
{
    std::vector<uint8_t> buf(n);
    if (!safeRead(p, buf.data(), n)) return "\"\"";
    return hexOf(buf.data(), n);
}

std::string num(double v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

std::string str(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20 || c >= 0x7F) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

// An MSVC 7.1 std::string whose buffer starts at `at` (size +0x10, capacity +0x14): printable text, or "".
std::string msvcString(uintptr_t at)
{
    uint32_t size = (uint32_t)u32(at + 0x10), cap = (uint32_t)u32(at + 0x14);
    if (size == 0 || size > 200 || cap < size || cap < 15 || cap > 0x10000) return std::string();
    uintptr_t chars = cap < 16 ? at : u32(at);
    char buf[201];
    if (!safeRead(chars, buf, size)) return std::string();
    for (uint32_t i = 0; i < size; ++i)
        if ((unsigned char)buf[i] < 0x20 || (unsigned char)buf[i] >= 0x7F) return std::string();
    return std::string(buf, size);
}

// Every MSVC string inside [base, base+n): [[offset, "text"], ...].
std::string stringsIn(uintptr_t base, size_t n)
{
    std::string s;
    for (size_t off = 0; off + 0x18 <= n; off += 4) {
        std::string t = msvcString(base + off);
        if (t.empty()) continue;
        char k[16];
        std::snprintf(k, sizeof(k), "[%u,", (unsigned)off);
        s += std::string(s.empty() ? "" : ",") + k + str(t) + "]";
    }
    return "[" + s + "]";
}

std::string templateName(uintptr_t obj)
{
    uintptr_t tmpl = u32(obj + bfv::OBJ_TEMPLATE);
    if (!ptrSane(tmpl)) return std::string();
    std::string s = msvcString(tmpl + bfv::TMPL_NAME);
    return s.empty() ? msvcString(tmpl + 0x2C) : s;
}

int netIdOf(uintptr_t obj)
{
    uintptr_t net = u32(obj + bfv::OBJ_NETWORKABLE);
    if (!ptrSane(net)) return -1;
    uint16_t id = 0;
    return safeRead(net + bfv::NET_ID, &id, 2) ? id : -1;
}

uintptr_t rootOf(uintptr_t obj)
{
    uintptr_t r = obj;
    for (int d = 0; d < 16; ++d) {
        uintptr_t p = u32(r + bfv::OBJ_PARENT);
        if (!ptrSane(p)) break;
        r = p;
    }
    return r;
}

std::string chainOf(uintptr_t obj)
{
    std::string s;
    uintptr_t r = obj;
    for (int d = 0; d < 16; ++d) {
        uintptr_t p = u32(r + bfv::OBJ_PARENT);
        if (!ptrSane(p)) break;
        s += std::string(s.empty() ? "" : ",") + hx(p);
        r = p;
    }
    return "[" + s + "]";
}

bool posOf(uintptr_t obj, float p[3]) { return safeRead(obj + bfv::OBJ_POS, p, 12) && std::isfinite(p[0]) && std::isfinite(p[2]); }

float dist(const float a[3], const float b[3])
{
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The BFPlayer behind an IPlayer pointer (the list and fireBarrel may hand an interface subobject).
uintptr_t bfPlayerOf(uintptr_t p)
{
    uintptr_t vt = u32(p);
    if (vt == VT_PLAYER) return p;
    if (vt == VT_PLAYER_4) return p - 4;
    if (vt == VT_PLAYER_8) return p - 8;
    return 0;
}

uintptr_t playerManager()
{
    uintptr_t pm = u32(bfv::PLAYERMGR_GLOBAL);
    return ptrSane(pm) && u32(pm) == VT_PlayerManager.va ? pm : 0;
}

uintptr_t localPlayer()
{
    uintptr_t pm = playerManager(), local = 0;
    if (pm) safeVcall0(pm, bfv::VSLOT_LOCAL_PLAYER, &local);
    return local;
}

// In-order successor in an MSVC _Tree whose header is the sentinel: left +0, parent +4, right +8.
uintptr_t treeSucc(uintptr_t node, uintptr_t header)
{
    uintptr_t right = u32(node + 8);
    if (right != header) {
        node = right;
        for (int d = 0; d < 64; ++d) {
            uintptr_t l = u32(node + 0);
            if (l == header || !ptrSane(l)) break;
            node = l;
        }
        return node;
    }
    uintptr_t p = u32(node + 4);
    for (int d = 0; d < 64 && p != header && ptrSane(p) && node == u32(p + 8); ++d) {
        node = p;
        p = u32(p + 4);
    }
    return ptrSane(p) ? p : header;
}

// ---- threads -----------------------------------------------------------------------------
struct Kind { const char* name; long count; DWORD tids[4]; int ntid; };
std::mutex g_tMu;
Kind g_kinds[3] = { { "event", 0, {}, 0 }, { "mouseState", 0, {}, 0 }, { "mouseData", 0, {}, 0 } };
double g_lastThreadReport = 0;

void noteThread(int k)
{
    DWORD tid = GetCurrentThreadId();
    double t = nowS();
    std::string lines;
    {
        std::lock_guard<std::mutex> lock(g_tMu);
        Kind& kd = g_kinds[k];
        kd.count++;
        bool seen = false;
        for (int i = 0; i < kd.ntid; ++i) seen |= kd.tids[i] == tid;
        if (!seen && kd.ntid < 4) kd.tids[kd.ntid++] = tid;
        if (t - g_lastThreadReport < 10.0) return;
        double span = g_lastThreadReport > 0 ? t - g_lastThreadReport : t;
        g_lastThreadReport = t;
        for (Kind& q : g_kinds) {
            std::string tids;
            for (int i = 0; i < q.ntid; ++i) tids += (i ? "," : "") + std::to_string(q.tids[i]);
            lines += "{\"probe\":\"thread\",\"t\":" + num(t) + ",\"what\":\"" + q.name + "\",\"tids\":[" + tids
                + "],\"perSec\":" + num(span > 0 ? q.count / span : 0) + "}\n";
            q.count = 0;
        }
    }
    lines.pop_back();
    out(lines);
    outFlush();
}

// ---- the 5 s dump --------------------------------------------------------------------------
int g_evSeen[256];
bool g_stackDone = false, g_helloDone = false, g_animDone = false;
double g_lastDump = -1e9, g_lastScoreScan = -1e9;
int g_scoreScans = 0;

void hello(double t)
{
    char b[512];
    std::snprintf(b, sizeof(b),
        "{\"probe\":\"hello\",\"t\":%s,\"v\":2,\"build\":\"%s %s\",\"armorVt\":%s,\"compHead\":%u,\"armorIid\":%u,"
        "\"scoreGlobal\":%s,\"animGlobal\":%s,\"fireBarrel\":%s}",
        num(t).c_str(), __DATE__, __TIME__, hx(VT_Armor.va).c_str(), (unsigned)bfv::COMP_MAP_HEAD,
        (unsigned)bfv::ARMOR_IID, hx(bfv::SCOREMGR_GLOBAL).c_str(), hx(bfv::ANIM_ASM_GLOBAL).c_str(),
        hx(bfv::FIRE_BARREL_FN).c_str());
    out(b);
}

struct Walked { uintptr_t obj, vt, parent; };

// Every object in the object manager's list (stage 1's hash_map list, bfv_addresses.h).
int walkObjects(std::vector<Walked>& all, unsigned& listSize)
{
    all.clear();
    listSize = 0;
    uintptr_t mgr = u32(bfv::OBJMGR_GLOBAL);
    if (!ptrSane(mgr)) return -1;
    uintptr_t head = u32(mgr + bfv::OBJMGR_LIST_HEAD);
    if (!ptrSane(head)) return -1;
    listSize = (unsigned)u32(mgr + bfv::OBJMGR_LIST_SIZE);
    const unsigned guardMax = listSize + 64 < 60000 ? listSize + 64 : 60000;
    uintptr_t node = u32(head + bfv::LIST_NEXT);
    for (unsigned g = 0; node != head && ptrSane(node) && g < guardMax; ++g) {
        uintptr_t obj = u32(node + bfv::LIST_OBJECT);
        node = u32(node + bfv::LIST_NEXT);
        if (!ptrSane(obj)) continue;
        all.push_back(Walked{ obj, u32(obj), u32(obj + bfv::OBJ_PARENT) });
    }
    return (int)all.size();
}

void dumpComponents(double t, uintptr_t obj, int net)
{
    uintptr_t header = u32(obj + bfv::COMP_MAP_HEAD);
    std::string items;
    int n = 0;
    if (ptrSane(header)) {
        uintptr_t node = u32(header + 0);   // leftmost
        while (node != header && ptrSane(node) && n < MAX_COMP) {
            uintptr_t key = u32(node + bfv::COMP_NODE_KEY), value = u32(node + bfv::COMP_NODE_VALUE);
            uintptr_t vt = u32(value);
            items += std::string(n ? "," : "") + "[" + std::to_string((unsigned)key) + "," + hx(value) + "," + hx(vt) + ","
                + hexBytes(value, vt == VT_Armor.va ? ARMOR_BYTES : COMP_BYTES) + "]";
            ++n;
            node = treeSucc(node, header);
        }
    }
    out("{\"probe\":\"comp\",\"t\":" + num(t) + ",\"a\":" + hx(obj) + ",\"net\":" + std::to_string(net) + ",\"head\":"
        + hx(header) + ",\"n\":" + std::to_string(n) + ",\"items\":[" + items + "]}");
}

void dumpScore(double t)
{
    uintptr_t sm = u32(bfv::SCOREMGR_GLOBAL);
    uintptr_t vt = ptrSane(sm) ? u32(sm) : 0;
    std::string line = "{\"probe\":\"score\",\"t\":" + num(t) + ",\"sm\":" + hx(sm) + ",\"vt\":" + hx(vt) + ",\"raw\":"
        + (ptrSane(sm) ? hexBytes(sm, SCORE_BYTES) : std::string("\"\""));
    uintptr_t target = vt == VT_ScoreManager.va ? sm : 0;
    if (!target && g_scoreScans < 12 && t - g_lastScoreScan >= 10.0) {
        // The global holds no ScoreManager: find one by its vtable (~0.1 s stall), then report it.
        g_lastScoreScan = t;
        ++g_scoreScans;
        static uintptr_t hits[16];
        int n = 0;
        uint8_t* addr = (uint8_t*)0x00010000;
        MEMORY_BASIC_INFORMATION mbi;
        while (addr < (uint8_t*)0x7FFE0000 && n < 16) {
            if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
            uint8_t* next = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && mbi.RegionSize >= 8 && !(mbi.Protect & PAGE_GUARD)
                && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)
                && !inImage((uintptr_t)mbi.BaseAddress))
                n = scanRegion((const uint8_t*)mbi.BaseAddress, mbi.RegionSize, VT_ScoreManager.va, hits, n, 16);
            if (next <= addr) break;
            addr = next;
        }
        std::string h;
        for (int i = 0; i < n; ++i) h += std::string(i ? "," : "") + "[" + hx(hits[i]) + "," + hexBytes(hits[i], SCORE_BYTES) + "]";
        line += ",\"scan\":[" + h + "]";
        if (n) target = hits[0];
    }
    if (target) {
        static uintptr_t found[64];
        int nf = 0;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)0x00400000;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(0x00400000 + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            uintptr_t lo = 0x00400000 + sec[i].VirtualAddress;
            nf = scanGlobals(lo, lo + sec[i].Misc.VirtualSize, target, found, nf, 64);
        }
        std::string g;
        for (int i = 0; i < nf; ++i) g += std::string(i ? "," : "") + hx(found[i]);
        line += ",\"globals\":[" + g + "]";
    }
    out(line + "}");
}

void dumpAnim(double t)
{
    uintptr_t sm = u32(bfv::ANIM_ASM_GLOBAL);
    if (!ptrSane(sm)) return;
    uintptr_t b = u32(sm + bfv::ASM_STATES), e = u32(sm + bfv::ASM_STATES + 4);
    if (!ptrSane(b) || e < b || (e - b) / 4 > (1u << 18) || e == b) return;
    const unsigned n = (unsigned)((e - b) / 4);
    g_animDone = true;
    out("{\"probe\":\"animHead\",\"t\":" + num(t) + ",\"asm\":" + hx(sm) + ",\"raw\":" + hexBytes(sm, 0x80) + ",\"n\":"
        + std::to_string(n) + ",\"first\":" + hexBytes(u32(b), STATE_SCAN) + "}");
    std::string items;
    unsigned inLine = 0, from = 0;
    for (unsigned i = 0; i < n; ++i) {
        uintptr_t st = u32(b + 4 * i);
        if (ptrSane(st))
            items += std::string(inLine ? "," : "") + "[" + std::to_string(i) + "," + std::to_string((unsigned)u32(st + bfv::STATE_FLAGS))
                + "," + stringsIn(st, STATE_SCAN) + "]";
        else
            items += std::string(inLine ? "," : "") + "[" + std::to_string(i) + ",null,[]]";
        if (++inLine == 400 || i + 1 == n) {
            out("{\"probe\":\"anim\",\"t\":" + num(t) + ",\"from\":" + std::to_string(from) + ",\"states\":[" + items + "]}");
            items.clear();
            inLine = 0;
            from = i + 1;
        }
    }
}

void dumpAll(double t)
{
    const double t0 = nowS();
    uintptr_t pm = playerManager(), local = 0, veh = 0, vehVt = 0;
    if (pm) safeVcall0(pm, bfv::VSLOT_LOCAL_PLAYER, &local);
    if (local) safeVcall0(local, bfv::VSLOT_PLAYER_VEHICLE, &veh);
    if (veh) vehVt = u32(veh);
    out("{\"probe\":\"local\",\"t\":" + num(t) + ",\"pm\":" + hx(pm) + ",\"local\":" + hx(local) + ",\"veh\":" + hx(veh)
        + ",\"vehVt\":" + hx(vehVt) + ",\"localId\":" + std::to_string((int)u32(local + bfv::PLAYER_ID)) + "}");
    float here[3] = { 0, 0, 0 };
    bool haveHere = ptrSane(veh) && posOf(rootOf(veh), here);

    // Players, from the player manager's list.
    int nPl = 0;
    uintptr_t head = pm ? u32(pm + bfv::PM_LIST_HEAD) : 0;
    if (ptrSane(head)) {
        uintptr_t node = u32(head);
        for (int g = 0; node != head && ptrSane(node) && g < 300; ++g) {
            uintptr_t pl = bfPlayerOf(u32(node + bfv::PM_NODE_PLAYER));
            node = u32(node);
            if (!pl) continue;
            out("{\"probe\":\"pl\",\"t\":" + num(t) + ",\"a\":" + hx(pl) + ",\"local\":" + (pl == local ? "1" : "0")
                + ",\"raw\":" + hexBytes(pl, PLAYER_BYTES) + "}");
            ++nPl;
        }
    }

    uintptr_t mgr = u32(bfv::OBJMGR_GLOBAL);
    out("{\"probe\":\"mgr\",\"t\":" + num(t) + ",\"mgr\":" + hx(mgr) + ",\"raw\":" + hexBytes(mgr, 0x140) + "}");

    std::vector<Walked> all;
    unsigned listSize = 0;
    const int walked = walkObjects(all, listSize);

    // Roots first: soldiers and vehicles with a net id, and where each root is.
    std::map<uintptr_t, int> rootNet;          // root -> net id (sampled soldiers and vehicles)
    std::map<uintptr_t, float> rootDist;       // root -> distance from the local player's root
    int nSol = 0, nVeh = 0, nCp = 0, nFa = 0, nEng = 0, nPart = 0, nMus = 0;
    for (const Walked& w : all) {
        if (w.parent || (w.vt != VT_SOLDIER && w.vt != VT_PCO)) continue;
        const int net = netIdOf(w.obj);
        float p[3];
        if (net < 0 || !posOf(w.obj, p) || (std::fabs(p[0]) < 2.0f && std::fabs(p[2]) < 2.0f)) continue;
        rootNet[w.obj] = net;
        rootDist[w.obj] = haveHere ? dist(p, here) : 0.0f;
        if (w.vt == VT_SOLDIER) {
            out("{\"probe\":\"sol\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"net\":" + std::to_string(net) + ",\"raw\":"
                + hexBytes(w.obj, SOL_BYTES) + "}");
            ++nSol;
        } else {
            out("{\"probe\":\"veh\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"net\":" + std::to_string(net) + ",\"tmpl\":"
                + str(templateName(w.obj)) + ",\"raw\":" + hexBytes(w.obj, VEH_BYTES) + "}");
            ++nVeh;
        }
        dumpComponents(t, w.obj, net);
    }

    // Everything else by class, placed by its root.
    for (const Walked& w : all) {
        if (w.vt == VT_ControlPoint.va) {
            uintptr_t tm = u32(w.obj + bfv::OBJ_TEMPLATE), tm2 = u32(w.obj + bfv::CP_TEMPLATE);
            std::string line = "{\"probe\":\"cp\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"gid\":"
                + std::to_string((unsigned)u32(w.obj + bfv::OBJ_ID)) + ",\"net\":" + std::to_string(netIdOf(w.obj)) + ",\"tmpl\":"
                + str(templateName(w.obj)) + ",\"raw\":" + hexBytes(w.obj, CP_BYTES) + ",\"tm\":" + hx(tm) + ",\"tmRaw\":"
                + hexBytes(tm, TMPL_BYTES) + ",\"tmStr\":" + stringsIn(tm, TMPL_BYTES);
            if (tm2 != tm && ptrSane(tm2))
                line += ",\"tm2\":" + hx(tm2) + ",\"tm2Raw\":" + hexBytes(tm2, TMPL_BYTES) + ",\"tm2Str\":" + stringsIn(tm2, TMPL_BYTES);
            out(line + "}");
            ++nCp;
            continue;
        }
        if (w.vt == VT_MusicPlayer.va) {
            uintptr_t r = rootOf(w.obj);
            out("{\"probe\":\"mus\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"root\":" + hx(r) + ",\"net\":"
                + std::to_string(netIdOf(r)) + ",\"raw\":" + hexBytes(w.obj, MUS_BYTES) + "}");
            ++nMus;
            continue;
        }
        if (!w.parent) continue;
        uintptr_t r = rootOf(w.obj);
        auto rn = rootNet.find(r);
        if (rn == rootNet.end()) continue;
        const float d = rootDist[r];
        if ((w.vt == VT_FireArms.va || w.vt == VT_HandFireArms.va) && d <= FA_RANGE && nFa < MAX_FA) {
            out("{\"probe\":\"fa\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"vt\":" + hx(w.vt) + ",\"tmpl\":"
                + str(templateName(w.obj)) + ",\"root\":" + std::to_string(rn->second) + ",\"chain\":" + chainOf(w.obj) + ",\"raw\":"
                + hexBytes(w.obj, FA_BYTES) + "}");
            ++nFa;
        }
        if (w.vt == VT_Engine.va && d <= ENG_RANGE && nEng < MAX_ENG) {
            uintptr_t pe = u32(w.obj + bfv::ENG_PHYS);
            out("{\"probe\":\"eng\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"tmpl\":" + str(templateName(w.obj)) + ",\"root\":"
                + std::to_string(rn->second) + ",\"raw\":" + hexBytes(w.obj, ENG_BYTES) + ",\"pe\":" + hx(pe) + ",\"peVt\":"
                + hx(u32(pe)) + ",\"peRaw\":" + hexBytes(pe, PE_BYTES) + "}");
            ++nEng;
        }
        if (u32(r) == VT_PCO && d <= PART_RANGE && nPart < MAX_PART) {
            out("{\"probe\":\"part\",\"t\":" + num(t) + ",\"a\":" + hx(w.obj) + ",\"vt\":" + hx(w.vt) + ",\"tmpl\":"
                + str(templateName(w.obj)) + ",\"tm\":" + hx(u32(w.obj + bfv::OBJ_TEMPLATE)) + ",\"net\":"
                + std::to_string(netIdOf(w.obj)) + ",\"root\":" + std::to_string(rn->second) + ",\"parent\":" + hx(w.parent)
                + ",\"chain\":" + chainOf(w.obj) + ",\"flags\":" + hx(u32(w.obj + bfv::OBJ_FLAGS)) + ",\"rel\":"
                + hexBytes(u32(w.obj + bfv::OBJ_REL_PTR), 64) + ",\"abs\":" + hexBytes(w.obj + bfv::OBJ_ROWS, 64) + "}");
            ++nPart;
        }
    }

    dumpScore(t);
    if (!g_animDone) dumpAnim(t);
    char b[320];
    std::snprintf(b, sizeof(b),
        "{\"probe\":\"dumpEnd\",\"t\":%s,\"ms\":%s,\"list\":%u,\"walked\":%d,\"players\":%d,\"sol\":%d,\"veh\":%d,\"cp\":%d,"
        "\"fa\":%d,\"eng\":%d,\"part\":%d,\"mus\":%d}",
        num(t).c_str(), num((nowS() - t0) * 1000.0).c_str(), listSize, walked, nPl, nSol, nVeh, nCp, nFa, nEng, nPart, nMus);
    out(b);
    outFlush();
}

// ---- the local player's changing fields (hit mark, aim), every poll ---------------------------
uint8_t g_lpPrev[PLAYER_BYTES];
uintptr_t g_lpAddr = 0;

void localDiff()
{
    uintptr_t lp = localPlayer();
    if (!ptrSane(lp)) { g_lpAddr = 0; return; }
    uint8_t cur[PLAYER_BYTES];
    if (!safeRead(lp, cur, PLAYER_BYTES)) return;
    const double t = nowS();
    if (lp != g_lpAddr) {
        g_lpAddr = lp;
        std::memcpy(g_lpPrev, cur, PLAYER_BYTES);
        out("{\"probe\":\"lpFull\",\"t\":" + num(t) + ",\"a\":" + hx(lp) + ",\"raw\":" + hexOf(cur, PLAYER_BYTES) + "}");
        return;
    }
    std::string d;
    int n = 0;
    for (size_t off = 0; off < PLAYER_BYTES && n < 48; off += 4) {
        if (std::memcmp(cur + off, g_lpPrev + off, 4) == 0) continue;
        char k[16];
        std::snprintf(k, sizeof(k), "[%u,", (unsigned)off);
        d += std::string(n ? "," : "") + k + hexOf(cur + off, 4) + "]";
        ++n;
    }
    std::memcpy(g_lpPrev, cur, PLAYER_BYTES);
    if (n) out("{\"probe\":\"lp\",\"t\":" + num(t) + ",\"d\":[" + d + "]}");
}

// ---- the fire hook ----------------------------------------------------------------------------
typedef void(__thiscall* FireBarrelFn)(void* self, void* player, void* mat, int index);
std::mutex g_fireMu;
std::map<uintptr_t, int> g_fireCalls;   // FireArms -> calls logged, for the first-three raw dumps
long g_fireLines = 0;

void noteFire(uintptr_t fa, uintptr_t player, uintptr_t mat, int index)
{
    const double t = nowS();
    float m[16];
    std::string ms = "null";
    if (safeRead(mat, m, sizeof(m))) {
        ms = "[";
        char b[32];
        for (int i = 0; i < 16; ++i) { std::snprintf(b, sizeof(b), "%s%.5g", i ? "," : "", m[i]); ms += b; }
        ms += "]";
    }
    uintptr_t bf = ptrSane(player) ? bfPlayerOf(player) : 0;
    const int pid = bf ? (int)u32(bf + bfv::PLAYER_ID) : -1;
    const bool local = bf && bf == localPlayer();
    uintptr_t root = rootOf(fa);
    int calls;
    {
        std::lock_guard<std::mutex> lock(g_fireMu);
        calls = ++g_fireCalls[fa];
        ++g_fireLines;
    }
    std::string line = "{\"probe\":\"fb\",\"t\":" + num(t) + ",\"fa\":" + hx(fa) + ",\"vt\":" + hx(u32(fa)) + ",\"tmpl\":"
        + str(templateName(fa)) + ",\"root\":" + std::to_string(netIdOf(root)) + ",\"rootVt\":" + hx(u32(root)) + ",\"player\":"
        + hx(player) + ",\"playerVt\":" + hx(u32(player)) + ",\"pid\":" + std::to_string(pid) + ",\"local\":" + (local ? "1" : "0")
        + ",\"idx\":" + std::to_string(index) + ",\"m\":" + ms;
    if (calls <= 3) line += ",\"raw\":" + hexBytes(fa, FA_BYTES);
    out(line + "}");
}

void __fastcall hook_fireBarrel(void* self, void* /*edx*/, void* player, void* mat, int index)
{
    noteFire((uintptr_t)self, (uintptr_t)player, (uintptr_t)mat, index);
    ((FireBarrelFn)bfv::FIRE_BARREL_FN)(self, player, mat, index);
}

bool bytesAt(uintptr_t va, const char* hex)
{
    uint8_t want[16];
    size_t n = 0;
    for (const char* p = hex; *p && n < sizeof(want);) {
        while (*p == ' ') ++p;
        if (!p[0] || !p[1]) break;
        char b[3] = { p[0], p[1], 0 };
        want[n++] = (uint8_t)std::strtoul(b, nullptr, 16);
        p += 2;
    }
    uint8_t have[16];
    return n && safeRead(va, have, n) && std::memcmp(have, want, n) == 0;
}

}  // namespace

bool probe_installFireHook()
{
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) return false;
    const BfvSite* sites[] = { &SITE_fireBarrelCall1, &SITE_fireBarrelCall2, &SITE_fireBarrelCall3, &SITE_fireBarrelCall4,
        &SITE_fireBarrelCall5, &SITE_fireBarrelCall6 };
    if (!bytesAt(SITE_fireBarrelEntry.va, SITE_fireBarrelEntry.bytes)) {
        plusLog("probe: fireBarrel's entry differs; fire hook not installed");
        return false;
    }
    for (const BfvSite* s : sites) {
        uint8_t* site = (uint8_t*)s->va;
        if (!bytesAt(s->va, s->bytes) || site[0] != 0xE8
            || (uintptr_t)(site + 5) + (uintptr_t)(*(int32_t*)(site + 1)) != bfv::FIRE_BARREL_FN) {
            plusLog("probe: fireBarrel call site 0x%08X differs; fire hook not installed", (unsigned)s->va);
            return false;
        }
    }
    for (const BfvSite* s : sites) {
        uint8_t* site = (uint8_t*)s->va;
        DWORD old;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
        *(int32_t*)(site + 1) = (int32_t)((uintptr_t)&hook_fireBarrel - (uintptr_t)(site + 5));
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    plusLog("probe: fire hook on fireBarrel's six call sites");
    return true;
}

void probe_onInputPoll(int kind)
{
    noteThread(kind == 0 ? 1 : 2);
    if (kind == 0) localDiff();
}

void probe_onEvent(void* ev)
{
    noteThread(0);
    double t = nowS();
    if (!g_helloDone) { g_helloDone = true; hello(t); }

    if (!g_stackDone) {
        g_stackDone = true;
        void* frames[16];
        USHORT n = CaptureStackBackTrace(0, 16, frames, nullptr);
        std::string f;
        for (USHORT i = 0; i < n; ++i) f += std::string(i ? "," : "") + hx((uintptr_t)frames[i]);
        out("{\"probe\":\"stack\",\"t\":" + num(t) + ",\"frames\":[" + f + "]}");
    }

    uintptr_t type = 0;
    if (safeVcall0((uintptr_t)ev, 0, &type) && type < 256 && g_evSeen[type] < 20) {
        g_evSeen[type]++;
        out("{\"probe\":\"ev\",\"t\":" + num(t) + ",\"type\":" + std::to_string((unsigned)type) + ",\"hex\":"
            + hexBytes((uintptr_t)ev, 96) + "}");
    }

    if (t - g_lastDump >= 5.0) {
        g_lastDump = t;
        dumpAll(t);
    }
}
