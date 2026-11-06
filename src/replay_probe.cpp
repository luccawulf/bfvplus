// The address-finding probe (see replay_probe.h). Everything it writes is raw: the matching
// against the lab server recorder's file happens offline (probe_match.py in the viewer repo).
//
// Lines in logs\bfvplus_probe.log (JSON, one per line; t = seconds since the probe started):
//   thread   every 10 s per callback kind: the thread ids seen and calls per second
//   stack    once: the return addresses above the readEvent hook
//   ev       the first 20 events of each type: 96 bytes from the event pointer
//   local    every 5 s: the player manager, the local player and the object he controls
//   player   every 5 s: every live BFPlayer (heap scan for its vtable), 0x200 bytes
//   obj      every 5 s: every live PlayerControlObject and BFSoldier (heap scan), 0x300 bytes
//            from the object's start, its body's transform region, and every 30 s the first
//            0x40 bytes behind each heap pointer it holds
//   walk     every 5 s: the object manager's trees (header at mgr+0x54 and mgr+0xA4), each
//            node's +0x0C and +0x10 and the vtables they point at
//   globals  every 5 s: writable exe dwords that hold a player's address
//
// Reads are SEH-guarded through the small helpers below (functions with __try hold no C++
// objects). The heap scan stalls the game for a moment every 5 s: acceptable for a probe
// session, and the probe is off unless asked for.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "plus_log.h"
#include "replay_probe.h"

namespace {

const uintptr_t PLAYER_MGR_GLOBAL = 0x00D7D01C;   // sensitivity.cpp: playerMgr; vt[9] local, vt[14] vehicle
const uintptr_t OBJ_MGR_GLOBAL = 0x00D7D014;      // port notes, live 2026-06-03
const uintptr_t VT_PCO = 0x00B93D24;              // RTTI: PlayerControlObject, subobject +0
const uintptr_t VT_SOLDIER = 0x00B9033C;          // RTTI: BFSoldier, subobject +0
const uintptr_t VT_PLAYER = 0x00B5F70C;           // RTTI: BFPlayer, subobject +0
const unsigned VSLOT_LOCAL = 9, VSLOT_VEHICLE = 14;
const size_t OBJ_BYTES = 0x300, PLAYER_BYTES = 0x200, DEREF_BYTES = 0x40;
const uintptr_t OBJ_BODY_PTR = 0x280;             // capture.cpp: (object+0x160)+0x120 -> body
const int MAX_HITS = 4096;

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
        plusLog("probe: writing logs\\bfvplus_probe.log");
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

bool safeDword(uintptr_t p, uintptr_t* v)
{
    return safeRead(p, v, sizeof(*v));
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

// Find dwords equal to one of three vtables in [base, base+size). Appends to hits/kinds.
int scanRegion(const uint8_t* base, size_t size, uintptr_t* hits, int* kinds, int n)
{
    __try {
        const uint8_t* end = base + size - 4;
        for (const uint8_t* p = base; p <= end && n < MAX_HITS; p += 4) {
            uintptr_t v = *(const uintptr_t*)p;
            int k = v == VT_PCO ? 1 : v == VT_SOLDIER ? 2 : v == VT_PLAYER ? 3 : 0;
            if (k) { hits[n] = (uintptr_t)p; kinds[n] = k; ++n; }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// Writable exe dwords equal to any of `targets`.
int scanGlobals(uintptr_t lo, uintptr_t hi, const uintptr_t* targets, int nt, uintptr_t* found, uintptr_t* what, int cap)
{
    int n = 0;
    __try {
        for (uintptr_t p = lo; p + 4 <= hi && n < cap; p += 4) {
            uintptr_t v = *(const uintptr_t*)p;
            for (int i = 0; i < nt; ++i)
                if (v == targets[i]) { found[n] = p; what[n] = v; ++n; break; }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// In-order successor in an MSVC _Tree whose header is the nil sentinel (as capture.cpp walks
// the template tree): left +0, parent +4, right +8.
uintptr_t treeSucc(uintptr_t node, uintptr_t header)
{
    __try {
        uintptr_t right = *(const uintptr_t*)(node + 8);
        if (right != header) {
            node = right;
            uintptr_t l;
            while ((l = *(const uintptr_t*)(node + 0)) != header) {
                if (!ptrSane(l)) return header;
                node = l;
            }
            return node;
        }
        uintptr_t p = *(const uintptr_t*)(node + 4);
        while (p != header && node == *(const uintptr_t*)(p + 8)) {
            node = p;
            p = *(const uintptr_t*)(p + 4);
            if (p != header && !ptrSane(p)) return header;
        }
        return p;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return header; }
}

// ---- text helpers ------------------------------------------------------------------------
std::string hx(uintptr_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "\"0x%08X\"", (unsigned)v);
    return b;
}

std::string hexBytes(uintptr_t p, size_t n)
{
    std::vector<uint8_t> buf(n);
    if (!safeRead(p, buf.data(), n)) return "\"\"";
    std::string s = "\"";
    char b[4];
    for (uint8_t c : buf) { std::snprintf(b, sizeof(b), "%02x", c); s += b; }
    return s + "\"";
}

std::string num(double v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    return b;
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

// ---- dumps -------------------------------------------------------------------------------
int g_evSeen[256];
bool g_stackDone = false;
double g_lastDump = -1e9;
int g_dumpCount = 0;

void dumpWalk(double t, uintptr_t mgr, uintptr_t headOff)
{
    uintptr_t header = 0;
    std::string items;
    int n = 0;
    if (safeDword(mgr + headOff, &header) && ptrSane(header)) {
        uintptr_t node = 0;
        safeDword(header + 0, &node);   // leftmost
        while (node != header && ptrSane(node) && n < 5000) {
            uintptr_t a = 0, b = 0, va = 0, vb = 0;
            safeDword(node + 0x0C, &a);
            safeDword(node + 0x10, &b);
            if (ptrSane(a)) safeDword(a, &va);
            if (ptrSane(b)) safeDword(b, &vb);
            items += std::string(n ? "," : "") + "[" + hx(a) + "," + hx(va) + "," + hx(b) + "," + hx(vb) + "]";
            ++n;
            node = treeSucc(node, header);
        }
    }
    char off[16];
    std::snprintf(off, sizeof(off), "0x%X", (unsigned)headOff);
    out("{\"probe\":\"walk\",\"t\":" + num(t) + ",\"off\":\"" + off + "\",\"header\":" + hx(header) + ",\"n\":"
        + std::to_string(n) + ",\"items\":[" + items + "]}");
}

void dumpAll(double t)
{
    const bool deref = (g_dumpCount++ % 6) == 0;   // the pointer targets every 30 s

    uintptr_t pm = 0, local = 0, veh = 0, vehVt = 0;
    safeDword(PLAYER_MGR_GLOBAL, &pm);
    if (pm) safeVcall0(pm, VSLOT_LOCAL, &local);
    if (local) safeVcall0(local, VSLOT_VEHICLE, &veh);
    if (veh) safeDword(veh, &vehVt);
    out("{\"probe\":\"local\",\"t\":" + num(t) + ",\"pm\":" + hx(pm) + ",\"pmRaw\":" + hexBytes(pm, 0x100)
        + ",\"local\":" + hx(local) + ",\"veh\":" + hx(veh) + ",\"vehVt\":" + hx(vehVt) + "}");

    static uintptr_t hits[MAX_HITS];
    static int kinds[MAX_HITS];
    int n = 0;
    uint8_t* addr = (uint8_t*)0x00010000;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < (uint8_t*)0x7FFE0000 && n < MAX_HITS) {
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        uint8_t* next = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.RegionSize >= 8 && !(mbi.Protect & PAGE_GUARD)
            && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)
            && !inImage((uintptr_t)mbi.BaseAddress))
            n = scanRegion((const uint8_t*)mbi.BaseAddress, mbi.RegionSize, hits, kinds, n);
        if (next <= addr) break;
        addr = next;
    }

    std::vector<uintptr_t> players;
    for (int i = 0; i < n; ++i) {
        uintptr_t a = hits[i];
        if (kinds[i] == 3) {
            players.push_back(a);
            out("{\"probe\":\"player\",\"t\":" + num(t) + ",\"a\":" + hx(a) + ",\"raw\":" + hexBytes(a, PLAYER_BYTES) + "}");
            continue;
        }
        uintptr_t body = 0;
        safeDword(a + OBJ_BODY_PTR, &body);
        std::string line = "{\"probe\":\"obj\",\"t\":" + num(t) + ",\"a\":" + hx(a) + ",\"cls\":\""
            + (kinds[i] == 1 ? "pco" : "soldier") + "\",\"raw\":" + hexBytes(a, OBJ_BYTES) + ",\"body\":" + hx(body)
            + ",\"bodyRaw\":" + hexBytes(body + 0x80, 0x80);
        if (deref) {
            std::vector<uint8_t> raw(OBJ_BYTES);
            std::string d;
            if (safeRead(a, raw.data(), OBJ_BYTES)) {
                for (size_t off = 0; off + 4 <= OBJ_BYTES; off += 4) {
                    uintptr_t v;
                    std::memcpy(&v, raw.data() + off, 4);
                    if (!ptrSane(v) || inImage(v)) continue;
                    char key[16];
                    std::snprintf(key, sizeof(key), "\"0x%03X\":", (unsigned)off);
                    d += std::string(d.empty() ? "" : ",") + key + hexBytes(v, DEREF_BYTES);
                }
            }
            line += ",\"deref\":{" + d + "}";
        }
        out(line + "}");
    }

    uintptr_t mgr = 0;
    safeDword(OBJ_MGR_GLOBAL, &mgr);
    out("{\"probe\":\"mgr\",\"t\":" + num(t) + ",\"mgr\":" + hx(mgr) + ",\"raw\":" + hexBytes(mgr, 0x100) + "}");
    if (ptrSane(mgr)) { dumpWalk(t, mgr, 0x54); dumpWalk(t, mgr, 0xA4); }

    // Writable sections of the exe image: which globals point at a player.
    std::vector<uintptr_t> targets = players;
    if (local) targets.push_back(local);
    if (!targets.empty()) {
        static uintptr_t found[256], what[256];
        int nf = 0;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)0x00400000;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(0x00400000 + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            uintptr_t lo = 0x00400000 + sec[i].VirtualAddress;
            nf += scanGlobals(lo, lo + sec[i].Misc.VirtualSize, targets.data(), (int)targets.size(), found + nf, what + nf, 256 - nf);
        }
        std::string g;
        for (int i = 0; i < nf; ++i) g += std::string(i ? "," : "") + "[" + hx(found[i]) + "," + hx(what[i]) + "]";
        out("{\"probe\":\"globals\",\"t\":" + num(t) + ",\"hits\":[" + g + "]}");
    }
    out("{\"probe\":\"dumpEnd\",\"t\":" + num(t) + ",\"ms\":" + num((nowS() - t) * 1000.0) + ",\"hits\":"
        + std::to_string(n) + "}");
    outFlush();
}

}  // namespace

void probe_onInputPoll(int kind)
{
    noteThread(kind == 0 ? 1 : 2);
}

void probe_onEvent(void* ev)
{
    noteThread(0);
    double t = nowS();

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
