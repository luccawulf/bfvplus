// SSM capture for Battlefield Vietnam. Ported from bf42++ (BF1942).
//
// BFV uses the same Refractor GameEvent system as BF1942 (dice::bf::). Received
// events are created+deserialized one at a time by FUN_005b3a70 @ 0x005b3a70:
//   bool __stdcall readEvent(void* buffer, int* out_event)
// which reads the type id off the wire, creates the GameEvent, deserializes it, and
// returns it in *out_event. It is called from exactly one site (0x005b3c14, inside
// the packet-deserialize loop FUN_005b3b50). We patch that call (a `call rel32`, the
// same technique bfvplus uses for the AddBuddy button) to route through our hook,
// which calls the real reader and then logs the event it produced.
//
// Addresses are from static RE of retail BfVietnam.exe v1.21 (image base 0x00400000,
// MD5 0c54d62082bc59a793e52fc7004cd955). See SSM Work/BFV_PORT_NOTES.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "capture.h"
#include "keyhash.h"

bool g_captureEnabled = true;
bool g_dumpObjects = true;          // [capture] dumpObjects (default on)
int  g_dumpObjectsInterval = 30;    // [capture] dumpObjectsInterval seconds (0 = join only)

// ---- game addresses (BfVietnam.exe v1.21) ----------------------------------
static const uintptr_t ADDR_readEvent   = 0x005B3A70;  // bool __stdcall(void*, int*)
static const uintptr_t ADDR_call_site   = 0x005B3C14;  // the `call 0x5B3A70` we patch

typedef bool(__stdcall* readEvent_t)(void* buffer, int* outEvent);
static const readEvent_t realReadEvent = (readEvent_t)ADDR_readEvent;

// GameEvent::getType() is vtable[0] (__thiscall, returns the numeric GameEventID).
typedef int(__thiscall* getType_t)(void* thisptr);

// ---- log file --------------------------------------------------------------
static FILE* g_log = nullptr;
static bool  g_logFailed = false;
// The capture hook runs on the game thread; the F9 dump runs on its own thread.
// Both call logWrite, so serialize file access. Initialized in installCaptureHook
// before either can run.
static CRITICAL_SECTION g_logCs;

static void buildLogPath(char* out, size_t cap)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { _snprintf(out, cap, "logs\\bfvplus_server.log"); out[cap - 1] = 0; return; }
    for (DWORD i = n; i > 0; --i) { if (exe[i - 1] == '\\' || exe[i - 1] == '/') { exe[i] = 0; break; } }
    char dir[MAX_PATH];
    _snprintf(dir, sizeof(dir), "%slogs", exe); dir[sizeof(dir) - 1] = 0;
    CreateDirectoryA(dir, nullptr);
    _snprintf(out, cap, "%s\\bfvplus_server.log", dir); out[cap - 1] = 0;
}

static bool openLog()
{
    if (g_log) return true;
    if (g_logFailed) return false;
    char path[MAX_PATH];
    buildLogPath(path, sizeof(path));
    g_log = fopen(path, "w");
    if (!g_log) { g_logFailed = true; return false; }
    return true;
}

static void logWrite(const char* line)
{
    EnterCriticalSection(&g_logCs);
    if (g_log || openLog()) {
        fputs(line, g_log);
        fflush(g_log);
    }
    LeaveCriticalSection(&g_logCs);
}

static void timestamp(char* out, size_t cap)
{
    SYSTEMTIME st; GetLocalTime(&st);
    _snprintf(out, cap, "%04d-%02d-%02d_%02d-%02d-%02d-%03d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    out[cap - 1] = 0;
}

// ---- GameEventID -> name (BFV map, from RTTI; see BFV_PORT_NOTES.md) --------
static const char* eventName(int id)
{
    switch (id) {
        case 0x03: return "DebugEvent";
        case 0x04: return "SimulationEvent";
        case 0x05: return "CreateMultipleObjectsEvent";
        case 0x06: return "DestroyObjectEvent";
        case 0x07: return "CreateObjectEvent";
        case 0x08: return "CreatePlayerEvent";
        case 0x09: return "SetDefaultVehicleEvent";
        case 0x0a: return "SetVehicleEvent";
        case 0x0b: return "ExitVehicleEvent";
        case 0x0c: return "DestroyPlayerEvent";
        case 0x0d: return "SetPlayerNameEvent";
        case 0x0e: return "AutoReloadEvent";
        case 0x0f: return "ConnectionTypeEvent";
        case 0x10: return "UpdateRateEvent";
        case 0x11: return "CreatePlayerAckEvent";
        case 0x12: return "VoteEvent";
        case 0x13: return "MapEvent";
        case 0x14: return "ChallengeEvent";
        case 0x15: return "ChallengeResponseEvent";
        case 0x16: return "GameRulesEvent";
        case 0x17: return "WelcomeMsgEvent";
        case 0x18: return "ResetMapEvent";
        case 0x1a: return "ServerInfoEvent";
        case 0x1b: return "ServerInfoEvent2";
        case 0x1c: return "MusicListEvent";
        case 0x1d: return "CustomCombatEvent";
        case 0x1e: return "FlagHolderEvent";
        case 0x1f: return "SetTempCameraEvent";
        case 0x24: return "GameStatusEvent";
        case 0x25: return "ControlPointCapturedEvent";
        case 0x26: return "ReqScoutCameraEvent";
        case 0x27: return "SpecialGameEvent";
        case 0x28: return "ChatFragmentEvent";
        case 0x29: return "TimerSyncEvent";
        case 0x2a: return "ScoreMsgEvent";
        case 0x2b: return "PBFragmentEvent";
        case 0x2f: return "SpawnRequestEvent";
        case 0x30: return "StatsKillsEvent";
        case 0x31: return "StatsShotsEvent";
        case 0x32: return "StatsHitsEvent";
        case 0x33: return "SuicideEvent";
        case 0x34: return "DataBaseCompleteEvent";
        case 0x35: return "DatabaseCompleteAckEvent";
        case 0x36: return "SetLevelEvent";
        case 0x37: return "SessionIdEvent";
        case 0x38: return "SetSkinEvent";
        case 0x39: return "SetTeamEvent";
        case 0x3a: return "RadioMessageEvent";
        case 0x3b: return "TriggerObjectSpawnerEvent";
        case 0x3c: return "HitFromPosEvent";
        case 0x3d: return "RemoteCommandEvent";
        case 0x3e: return "ChangeKitEvent";
        default:   return "Unknown";
    }
}

// Append a JSON-escaped string read from a bounded C-string at src into dst.
static void appendEscaped(char* dst, size_t cap, const char* src, size_t maxsrc)
{
    size_t j = 0;
    for (size_t i = 0; i < maxsrc && src[i] && j + 2 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = (char)c; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't'; }
        else if (c < 0x20) { /* drop other control chars */ }
        else dst[j++] = (char)c;
    }
    dst[j] = 0;
}

// Field offsets are from the per-event deserialize functions (vtable[4]) recovered
// during RE and verified against a live capture — see SSM Work/BFV_PORT_NOTES.md.
// GameEvent base: vptr@0, seq@4, nextEvent@8; event-specific fields from +0xc.
static const int kDumpBytes = 48;

// Emit one JSONL line for a received event: timestamp, type id+name, sequence
// number, then structured fields for modeled events (object spawns, players, chat,
// server identity, map, scores) or a hex+ascii window for everything not yet decoded.
// SEH-guarded — a malformed pointer must never take the game down. No C++ objects
// with destructors live here, so __try is legal.
static void logServerEvent(void* event)
{
    __try {
        const uint8_t* p = (const uint8_t*)event;
        void** vt = *(void***)event;
        int id = ((getType_t)vt[0])(event);
        unsigned seq = *(const unsigned*)(p + 4);

        char ts[40]; timestamp(ts, sizeof(ts));
        char line[1024];
        int n = _snprintf(line, sizeof(line),
                          "{\"t\":\"%s\",\"event\":\"%s\",\"id\":%d,\"seq\":%u",
                          ts, eventName(id), id, seq);
        if (n < 0 || n > (int)sizeof(line) - 8) n = (int)sizeof(line) - 8;
        char esc[300];

        switch (id) {
            case 0x05: { // CreateMultipleObjectsEvent — RE pass: 0x05 is NOT the 0x07 layout (it packs a
                         // count + an array of objects), so dump the raw event object to recover the
                         // per-object structure; the proper loop-decode lands in the next build.
                const int M = 128;
                char hx[M * 3 + 1]; char as[M * 2 + 1]; int a2 = 0;
                for (int i = 0; i < M; i++) {
                    uint8_t b = p[i];
                    _snprintf(hx + i * 3, 4, "%02x ", b);
                    if (b == '"' || b == '\\') as[a2++] = '\\';
                    as[a2++] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
                }
                hx[M * 3] = 0; as[a2] = 0;
                n += _snprintf(line + n, sizeof(line) - n, ",\"raw\":\"%s\",\"ascii\":\"%s\"", hx, as);
                break;
            }
            case 0x07: { // CreateObjectEvent (single object)
                unsigned objId = *(const unsigned*)(p + 0x0c);
                unsigned tid   = *(const unsigned short*)(p + 0x10);
                unsigned hasPos = p[0x12];
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"objectID\":%u,\"templateID\":%u,\"hasPos\":%s",
                               objId, tid, hasPos ? "true" : "false");
                if (n < 0 || n > (int)sizeof(line) - 8) n = (int)sizeof(line) - 8;
                if (hasPos) {
                    float px = *(const float*)(p + 0x13), py = *(const float*)(p + 0x17), pz = *(const float*)(p + 0x1b);
                    float rh = *(const float*)(p + 0x1f), rp = *(const float*)(p + 0x23), rr = *(const float*)(p + 0x27);
                    n += _snprintf(line + n, sizeof(line) - n,
                                   ",\"pos\":[%.3f,%.3f,%.3f],\"rot\":[%.3f,%.3f,%.3f]",
                                   px, py, pz, rh, rp, rr);
                }
                break;
            }
            case 0x06: { // DestroyObjectEvent — object id only
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"objectID\":%u", *(const unsigned*)(p + 0x0c));
                break;
            }
            case 0x08: { // CreatePlayerEvent — name at +0x10 (32-byte field)
                appendEscaped(esc, sizeof(esc), (const char*)(p + 0x10), 32);
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"index\":%u,\"name\":\"%s\"", p[0x0c], esc);
                break;
            }
            case 0x0c: { // DestroyPlayerEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"index\":%u", p[0x0c]);
                break;
            }
            case 0x17: { // WelcomeMsgEvent — server MOTD at +0x10
                appendEscaped(esc, sizeof(esc), (const char*)(p + 0x10), 250);
                n += _snprintf(line + n, sizeof(line) - n, ",\"message\":\"%s\"", esc);
                break;
            }
            case 0x36: { // SetLevelEvent — level path at +0x0c
                appendEscaped(esc, sizeof(esc), (const char*)(p + 0x0c), 200);
                n += _snprintf(line + n, sizeof(line) - n, ",\"level\":\"%s\"", esc);
                break;
            }
            case 0x2a: { // ScoreMsgEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"scoreType\":%u,\"value\":%u",
                               *(const unsigned*)(p + 0x0c), p[0x10]);
                break;
            }
            case 0x04: { // SimulationEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"active\":%s,\"value\":%u",
                               p[0x0c] ? "true" : "false", *(const unsigned*)(p + 0x0d));
                break;
            }
            case 0x28: { // ChatFragmentEvent — fragment text@+0x13 (len@+0x12), sender@+0x10
                unsigned fragLen = p[0x12] & 0x0f;
                char frag[20];
                appendEscaped(frag, sizeof(frag), (const char*)(p + 0x13), fragLen > 16 ? 16 : fragLen);
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"first\":%s,\"sender\":%u,\"frag\":\"%s\"",
                               (p[0x0c] & 1) ? "true" : "false", p[0x10], frag);
                break;
            }
            case 0x3d: { // RemoteCommandEvent — server console command string@+0x14 (64-byte field)
                appendEscaped(esc, sizeof(esc), (const char*)(p + 0x14), 64);
                n += _snprintf(line + n, sizeof(line) - n, ",\"command\":\"%s\"", esc);
                break;
            }
            case 0x24: { // GameStatusEvent
                n += _snprintf(line + n, sizeof(line) - n, ",\"status\":%u", p[0x0c]);
                break;
            }
            case 0x39: { // SetTeamEvent
                n += _snprintf(line + n, sizeof(line) - n, ",\"index\":%u,\"team\":%u", p[0x0c], p[0x0d]);
                break;
            }
            case 0x38: { // SetSkinEvent
                n += _snprintf(line + n, sizeof(line) - n, ",\"index\":%u,\"skin\":%u", p[0x0c], p[0x0d]);
                break;
            }
            case 0x1a: { // ServerInfoEvent — three length-prefixed strings
                char s1[40], s2[40], s3[40];
                appendEscaped(s1, sizeof(s1), (const char*)(p + 0x0c), 17);
                appendEscaped(s2, sizeof(s2), (const char*)(p + 0x1d), 17);
                appendEscaped(s3, sizeof(s3), (const char*)(p + 0x2e), 17);
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"info\":[\"%s\",\"%s\",\"%s\"]", s1, s2, s3);
                break;
            }
            case 0x1b: { // ServerInfoEvent2 — one length-prefixed string
                appendEscaped(esc, sizeof(esc), (const char*)(p + 0x0c), 44);
                n += _snprintf(line + n, sizeof(line) - n, ",\"info\":\"%s\"", esc);
                break;
            }
            case 0x09:   // SetDefaultVehicleEvent
            case 0x0a: { // SetVehicleEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"index\":%u,\"vehicleID\":%u", p[0x0c], *(const unsigned short*)(p + 0x0d));
                break;
            }
            case 0x0b: { // ExitVehicleEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"index\":%u,\"flag\":%s", p[0x0c], p[0x0d] ? "true" : "false");
                break;
            }
            case 0x0d: { // SetPlayerNameEvent — name@+0x0c (len-prefixed), player@+0x31
                appendEscaped(esc, sizeof(esc), (const char*)(p + 0x0c), 33);
                n += _snprintf(line + n, sizeof(line) - n, ",\"index\":%u,\"name\":\"%s\"", p[0x31], esc);
                break;
            }
            case 0x13: { // MapEvent — two strings (map@+0x15, mod@+0x59)
                char m1[80], m2[80];
                appendEscaped(m1, sizeof(m1), (const char*)(p + 0x15), 64);
                appendEscaped(m2, sizeof(m2), (const char*)(p + 0x59), 64);
                n += _snprintf(line + n, sizeof(line) - n, ",\"map\":\"%s\",\"mod\":\"%s\"", m1, m2);
                break;
            }
            case 0x3e: { // ChangeKitEvent
                n += _snprintf(line + n, sizeof(line) - n, ",\"kit\":%u", p[0x0c]);
                break;
            }
            case 0x1e: { // FlagHolderEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"held\":%s,\"index\":%u", p[0x0c] ? "true" : "false", p[0x0d]);
                break;
            }
            case 0x25: { // ControlPointCapturedEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"controlPoint\":%u,\"team\":%u", p[0x0c], p[0x0d]);
                break;
            }
            case 0x2f: { // SpawnRequestEvent (shares the 2-byte layout)
                n += _snprintf(line + n, sizeof(line) - n, ",\"a\":%u,\"b\":%u", p[0x0c], p[0x0d]);
                break;
            }
            case 0x3a: { // RadioMessageEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"msgID\":%u,\"index\":%u", *(const unsigned short*)(p + 0x0c), p[0x0f]);
                break;
            }
            case 0x29: { // TimerSyncEvent
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"t1\":%u,\"t2\":%u",
                               *(const unsigned*)(p + 0x0c), *(const unsigned*)(p + 0x10));
                break;
            }
            case 0x3c: { // HitFromPosEvent
                n += _snprintf(line + n, sizeof(line) - n, ",\"a\":%u,\"b\":%u", p[0x0c], p[0x0d]);
                break;
            }
            case 0x12: { // VoteEvent (field order matches BF1942)
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"target\":%u,\"yes\":%u,\"no\":%u,\"required\":%u,\"voter\":%u",
                               p[0x0c], p[0x0d], p[0x0e], p[0x0f], p[0x10]);
                break;
            }
            case 0x33:   // SuicideEvent — no payload
                break;
            default: { // unmodeled: raw hex + ascii window (the next RE target)
                char hex[kDumpBytes * 3 + 1], asc[kDumpBytes * 2 + 1];
                int a = 0;
                for (int i = 0; i < kDumpBytes; i++) {
                    uint8_t b = p[i];
                    _snprintf(hex + i * 3, 4, "%02x ", b);
                    if (b == '"' || b == '\\') asc[a++] = '\\';
                    asc[a++] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
                }
                hex[kDumpBytes * 3] = 0; asc[a] = 0;
                n += _snprintf(line + n, sizeof(line) - n,
                               ",\"bytes\":\"%s\",\"ascii\":\"%s\"", hex, asc);
                break;
            }
        }
        if (n < 0 || n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
        line[n++] = '}'; line[n++] = '\n'; line[n] = 0;
        logWrite(line);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // bad read — skip this event rather than crash the game
    }
}

// ---- object roster (dumpObjects) -------------------------------------------
// Emit a NAME for every networked object, keyed by its wire objId, so the report
// can join names onto the positions captured in CreateObjectEvent.
//
// Live RE (x32dbg, game RUNNING - pausing the game disconnects us from the server)
// found the clean source: the ObjectTemplateManager (*(0x00D7D018), vtable 0x00B95334)
// holds at manager+0xC an RB-tree that maps templateID -> template. The full loaded
// template library is indexed here by the SAME numeric templateID the server puts in
// CreateObjectEvent, so a CreateObjectEvent's templateID looks straight up to a name.
// (Verified from the capture: each create has a unique, ~contiguous templateID while the
// objId is a small reused slot of ~65 values - so templateID, not objId, is the join key;
// the tree's contiguous keys 0..N == the templateID range == the library size.) Node:
//   _Left@0, _Parent@4, _Right@8, templateID(key)@0xC, template(value)@0x10.
// The header node (*(manager+0xC)) doubles as the nil sentinel - leaf children and the
// past-the-end iterator all point at it. The template name is an MSVC SSO std::string
// usually at template+0xC ("UAZ", "Harbour", "W_USKnife_l1") but at template+0x2C for a
// few classes ("FreeCameraInfo"), so we try both. Replaces the old heap-scan, which on
// big maps matched hundreds of inactive pooled sub-objects (e.g. O_AirTower).
static const uintptr_t OBJ_TEMPLATE_MGR = 0x00D7D018;  // global -> ObjectTemplateManager*
static const int TREE_HEADER_OFF = 0x0C;   // manager+0xC -> tree header (nil sentinel)
static const int NODE_LEFT   = 0x00;
static const int NODE_PARENT = 0x04;
static const int NODE_RIGHT  = 0x08;
static const int NODE_KEY    = 0x0C;       // templateID
static const int NODE_VALUE  = 0x10;       // template*

// Read an MSVC SSO std::string (the template name) into out[]. SEH-guarded.
static bool readSsoName(const void* strObj, char* out, size_t cap)
{
    __try {
        const uint8_t* s = (const uint8_t*)strObj;
        unsigned mysize = *(const unsigned*)(s + 0x10);
        unsigned myres  = *(const unsigned*)(s + 0x14);
        if (mysize == 0 || mysize > 200) return false;
        const char* chars = (myres < 16) ? (const char*)s : *(const char* const*)s;
        if (!chars) return false;
        size_t k = mysize < cap - 1 ? mysize : cap - 1;
        for (size_t i = 0; i < k; i++) {
            char c = chars[i];
            out[i] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? c : '?';
        }
        out[k] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A template's name SSO is at template+0xC for most classes, +0x2C for a few.
static bool readTemplateName(const void* tmpl, char* out, size_t cap)
{
    if ((uintptr_t)tmpl < 0x00010000 || (uintptr_t)tmpl >= 0x7FF00000) return false;
    const uint8_t* t = (const uint8_t*)tmpl;
    if (readSsoName(t + 0x0C, out, cap)) return true;
    if (readSsoName(t + 0x2C, out, cap)) return true;
    return false;
}

static bool ptrSane(uintptr_t p) { return p >= 0x00010000 && p < 0x7FF00000; }

// In-order RB-tree successor (MSVC _Tree style; the header node is the nil sentinel).
// SEH-guarded; returns header on past-the-end or any bad read.
static uintptr_t treeSucc(uintptr_t node, uintptr_t header)
{
    __try {
        uintptr_t right = *(const uintptr_t*)(node + NODE_RIGHT);
        if (right != header) {
            node = right;
            uintptr_t l;
            while ((l = *(const uintptr_t*)(node + NODE_LEFT)) != header) {
                if (!ptrSane(l)) return header;
                node = l;
            }
            return node;
        }
        uintptr_t p = *(const uintptr_t*)(node + NODE_PARENT);
        while (p != header && node == *(const uintptr_t*)(p + NODE_RIGHT)) {
            node = p;
            p = *(const uintptr_t*)(p + NODE_PARENT);
            if (p != header && !ptrSane(p)) return header;
        }
        return p;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return header; }
}

// Walk the templateID -> template tree (ObjectTemplateManager+0xC) and log one NAME per
// templateID. The report joins these onto CreateObjectEvent (templateID -> pos) by id.
void dumpObjects()
{
    char ts[40]; timestamp(ts, sizeof(ts));
    char rec[96];
    _snprintf(rec, sizeof(rec), "{\"event\":\"BfvObjectDump\",\"t\":\"%s\"}\n", ts);
    logWrite(rec);

    int total = 0;
    __try {
        uintptr_t mgr = *(const uintptr_t*)OBJ_TEMPLATE_MGR;
        uintptr_t header = ptrSane(mgr) ? *(const uintptr_t*)(mgr + TREE_HEADER_OFF) : 0;
        if (ptrSane(header)) {
            uintptr_t node = *(const uintptr_t*)(header + NODE_LEFT);   // leftmost (begin)
            int guard = 0;
            while (node != header && ptrSane(node) && guard++ < 20000) {
                unsigned templateId = *(const unsigned*)(node + NODE_KEY);
                const void* tmpl    = *(const void* const*)(node + NODE_VALUE);
                char name[128];
                if (!readTemplateName(tmpl, name, sizeof(name))) { name[0] = '?'; name[1] = 0; }
                char line[200];
                _snprintf(line, sizeof(line),
                          "{\"event\":\"BfvTemplate\",\"templateID\":%u,\"name\":\"%s\"}\n",
                          templateId, name);
                line[sizeof(line) - 1] = 0;
                logWrite(line);
                total++;
                node = treeSucc(node, header);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    _snprintf(rec, sizeof(rec), "{\"event\":\"BfvObjectDumpEnd\",\"count\":%d}\n", total);
    logWrite(rec);
}

// ---- live object snapshot (the bf42++ dumpObjects approach) -----------------
// The drivable VEHICLES never appear on the network CreateObjectEvent stream (their
// templateIDs are not sent), so - exactly like bf42++ - we read the LIVE objects from
// memory and log each one's NAME + WORLD POSITION inline. Live RE (x32dbg, game running)
// on real vehicles found the path: a PlayerControlObject starts with vtable 0x00B93C44;
// its template is at PCO+0x08 (name via readTemplateName); its physical "body" sub-object
// is pointed to from PCO+0x120, and the body's world-transform translation (x,y,z then
// w=1.0) sits at body+0xC4 - the same +0xC4 transform offset the SimpleObject RE found.
// Inactive pooled PCOs have no valid body transform and are filtered out by the w==1 +
// in-range check, which also kills the old air-tower-pool noise.
static const uintptr_t PCO_VTABLE     = 0x00B93C44;
static const int       PCO_TEMPLATE_OFF = 0x08;
static const int       PCO_BODY_OFF     = 0x120;   // PCO -> physical body sub-object
static const int       BODY_POS_OFF     = 0xC4;    // body -> transform translation (x,y,z,w)

// A real world-transform translation row reads as (x,y,z,1.0) with in-map coordinates.
static bool validTransform(uintptr_t addr, float* outXYZ)
{
    __try {
        float x = *(const float*)(addr + 0x00);
        float y = *(const float*)(addr + 0x04);
        float z = *(const float*)(addr + 0x08);
        float w = *(const float*)(addr + 0x0c);
        if (w < 0.99f || w > 1.01f) return false;            // translation row has w==1
        if (!(x == x && y == y && z == z)) return false;     // x==x rejects NaN
        if (x < -100000.f || x > 100000.f) return false;
        if (z < -100000.f || z > 100000.f) return false;
        if (y < -10000.f  || y > 100000.f) return false;
        if (x > -2.f && x < 2.f && z > -2.f && z < 2.f) return false;  // near-origin junk
        outXYZ[0] = x; outXYZ[1] = y; outXYZ[2] = z;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Resolve a PCO's world position: PCO+0x120 -> body -> body+0xC4 (verified). Fall back to
// scanning the PCO's pointer fields in case the body offset varies by vehicle type.
static bool pcoWorldPos(const uint8_t* pco, float* outXYZ)
{
    __try {
        uintptr_t body = *(const uintptr_t*)(pco + PCO_BODY_OFF);
        if (ptrSane(body) && validTransform(body + BODY_POS_OFF, outXYZ)) return true;
        for (int off = 0x40; off <= 0x178; off += 4) {
            uintptr_t b = *(const uintptr_t*)(pco + off);
            if (ptrSane(b) && validTransform(b + BODY_POS_OFF, outXYZ)) return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Scan one committed region for live PlayerControlObjects; log name+pos for each. Count.
static int scanRegionForLiveObjects(const uint8_t* base, size_t size)
{
    int found = 0;
    __try {
        const uint8_t* end = base + size - 4;
        for (const uint8_t* p = base; p < end; p += 4) {
            if (*(const uintptr_t*)p != PCO_VTABLE) continue;
            const void* tmpl = *(const void* const*)(p + PCO_TEMPLATE_OFF);
            char name[128];
            if (!readTemplateName(tmpl, name, sizeof(name))) continue;
            float pos[3];
            if (!pcoWorldPos(p, pos)) continue;     // also filters the inactive pool
            char line[256];
            _snprintf(line, sizeof(line),
                      "{\"event\":\"BfvObject\",\"name\":\"%s\",\"pos\":[%.3f,%.3f,%.3f]}\n",
                      name, pos[0], pos[1], pos[2]);
            line[sizeof(line) - 1] = 0;
            logWrite(line);
            found++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return found;
}

// Walk committed RW heap pages and log every live PlayerControlObject's name + world pos.
void dumpLiveObjects()
{
    char ts[40]; timestamp(ts, sizeof(ts));
    char rec[96];
    _snprintf(rec, sizeof(rec), "{\"event\":\"BfvLiveDump\",\"t\":\"%s\"}\n", ts);
    logWrite(rec);

    int total = 0;
    uint8_t* addr = (uint8_t*)0x00010000;
    uint8_t* limit = (uint8_t*)0x7FFE0000;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < limit) {
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        uint8_t* next = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.RegionSize >= 8 &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            total += scanRegionForLiveObjects((const uint8_t*)mbi.BaseAddress, mbi.RegionSize);
        }
        if (next <= addr) break;
        addr = next;
    }
    _snprintf(rec, sizeof(rec), "{\"event\":\"BfvLiveDumpEnd\",\"count\":%d}\n", total);
    logWrite(rec);
}

// Periodic LIVE snapshot (vehicles/soldiers move, so re-dump on an interval). The static
// templateID->name library (dumpObjects) is dumped once on join instead.
static DWORD WINAPI dumpTimerThread(LPVOID)
{
    while (g_dumpObjects && g_dumpObjectsInterval > 0) {
        Sleep((DWORD)g_dumpObjectsInterval * 1000);
        if (g_dumpObjects) dumpLiveObjects();
    }
    return 0;
}

// Our replacement for `call 0x5B3A70`. Same convention/signature as the real reader
// (__stdcall, callee cleans the two args), so the patched call site stays balanced.
static bool __stdcall hook_readEvent(void* buffer, int* outEvent)
{
    bool ok = realReadEvent(buffer, outEvent);
    if (ok && outEvent && *outEvent) {
        void* ev = (void*)(intptr_t)*outEvent;
        if (g_captureEnabled) logServerEvent(ev);

        // Emit the local player's BYTE-EXACT keyhash once - from field 1 of the gcd CD-key
        // response (MD5 of the CD key), not the framing-shifted over-read. The 100%-accurate
        // value the server actually records for you.
        static bool localKeyhashEmitted = false;
        if (g_captureEnabled && !localKeyhashEmitted && g_haveLocalKeyhash) {
            char line[176];
            _snprintf(line, sizeof(line),
                      "{\"event\":\"LocalKeyHash\",\"keyhash\":\"%s\","
                      "\"note\":\"byte-exact MD5(cdkey) from the gcd CD-key response (local player)\"}\n",
                      g_localKeyhash);
            line[sizeof(line) - 1] = 0;
            logWrite(line);
            localKeyhashEmitted = true;
        }
        // Auto-dump objects when a join completes (DataBaseCompleteEvent 0x34).
        __try {
            int id = ((getType_t)(*(void***)ev)[0])(ev);
            // On join (DataBaseComplete): dump the templateID->name library once, plus a
            // first live-object snapshot. Vehicles that spawn later are caught by the timer.
            if (id == 0x34 && g_dumpObjects) { dumpObjects(); dumpLiveObjects(); }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return ok;
}

void installCaptureHook()
{
    // Serialize log writes (game thread + F9 dump thread). Do this first - logWrite
    // is only reachable after the hook/thread below, which we set up here.
    InitializeCriticalSection(&g_logCs);

    // Only patch the binary we reverse-engineered (BfVietnam.exe v1.21, base 0x400000).
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) return;

    uint8_t* site = (uint8_t*)ADDR_call_site;
    if (site[0] != 0xE8) return;                                  // must be `call rel32`
    int32_t rel = *(int32_t*)(site + 1);
    uintptr_t target = (uintptr_t)(site + 5) + (uintptr_t)rel;
    if (target != ADDR_readEvent) return;                        // not the call we expect

    DWORD oldProtect;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    int32_t newRel = (int32_t)((uintptr_t)&hook_readEvent - (uintptr_t)(site + 5));
    *(int32_t*)(site + 1) = newRel;
    VirtualProtect(site, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 5);

    // Periodic object snapshots when enabled (bfvplus.ini [capture] dumpObjects).
    if (g_dumpObjects && g_dumpObjectsInterval > 0) {
        HANDLE h = CreateThread(nullptr, 0, dumpTimerThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
}
