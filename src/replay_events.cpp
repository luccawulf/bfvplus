// Event records (see replay_events.h): a port of the BFV cases of the server recorder's
// emit_event_json (core.c). Offsets are payload offsets (event object +12); the server's comments on
// each field are kept short here, and the server-side bookkeeping (kit and pool watches, who said
// what) is left out. Lines are built with snprintf in the same formats, and strings are escaped the
// server's way (\u00XX for control and high bytes), so a client line equals the server's.
#include "replay_events.h"
#include <cstdio>
#include <cstring>

namespace {

// BFV's createPlayer and chat offsets (core.c, REC_GAME_BFV).
const size_t CPE_NAME = 4, CPE_PID = 36, CPE_NET = 37, CPE_AI = 45;
const size_t CF_SENDER = 4, CF_TOTAL = 5, CF_PIECE = 6, CF_TEXT = 7;

struct Payload {
    const uint8_t* p;
    size_t n;
    uint8_t u8(size_t at) const { return at < n ? p[at] : 0; }
    uint16_t u16(size_t at) const { return (uint16_t)(u8(at) | (u8(at + 1) << 8)); }
    uint32_t u32(size_t at) const { return (uint32_t)u8(at) | ((uint32_t)u8(at + 1) << 8) | ((uint32_t)u8(at + 2) << 16) | ((uint32_t)u8(at + 3) << 24); }
    int32_t i32(size_t at) const { return (int32_t)u32(at); }
    float f32(size_t at) const { uint32_t v = u32(at); float f; std::memcpy(&f, &v, 4); return f; }
    // A NUL-terminated string of at most `max` bytes at `at`.
    std::string str(size_t at, size_t max) const
    {
        std::string s;
        for (size_t i = 0; i < max && u8(at + i); ++i) s += (char)u8(at + i);
        return s;
    }
};

// The server recorder's json_escape: \" and \\, and \u00XX for bytes below 0x20 and from 0x7f.
std::string esc(const std::string& in)
{
    std::string out;
    char b[8];
    for (unsigned char c : in) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20 || c >= 0x7f) { std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    return out;
}

bool numOk(float v) { return v >= -1e9f && v <= 1e9f; }

// The server recorder writes a player's control / kit only when it changes.
bool alreadySaid(uint32_t* table, int pid, uint32_t nid)
{
    if (pid < 0 || pid > 255 || !nid) return false;
    if (table[pid] == nid) return true;
    table[pid] = nid;
    return false;
}

}  // namespace

bool isServerInfo(int type) { return type == 0x1a; }

EventDecoder::EventDecoder(NameLookup lookup) : lookup_(std::move(lookup)) {}

bool EventDecoder::line(int type, const uint8_t* payload, size_t n, double t, std::string& out)
{
    const Payload P{ payload, payload ? n : 0 };
    char buf[1024];
    auto name = [&](uint32_t id) { return lookup_ ? esc(lookup_(id)) : std::string(); };
    buf[0] = 0;
    switch (type) {
        case 0x08: {  // CreatePlayerEvent: team 0, name[32] 4, pid 36, net ids 37.. (own, object, camera, kit), ai 45
            std::snprintf(buf, sizeof(buf),
                "{\"k\":\"e\",\"t\":%.3f,\"e\":\"createPlayer\",\"pid\":%u,\"name\":\"%s\",\"team\":%u,\"ai\":%u,"
                "\"netId\":%u,\"vehNetId\":%u,\"camNetId\":%u,\"kitNetId\":%u}",
                t, P.u8(CPE_PID), esc(P.str(CPE_NAME, 32)).c_str(), P.u8(0), P.u8(CPE_AI), P.u16(CPE_NET),
                P.u16(CPE_NET + 2), P.u16(CPE_NET + 4), P.u16(CPE_NET + 6));
            break;
        }
        case 0x0c:  // DestroyPlayerEvent: u8 pid
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"destroyPlayer\",\"pid\":%u}", t, P.u8(0));
            break;
        case 0x39:  // SetTeamEvent: u8 pid, u8 team
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"setTeam\",\"pid\":%u,\"team\":%u}",
                t, P.u8(0), P.u8(1));
            break;
        case 0x2a: {  // ScoreMsgEvent: kind 0, pid 4; for a kill or team kill: victim 5, weapon 6, body part 10
            int kind = P.i32(0);
            if (kind == 3 || kind == 6) {
                uint32_t wid = P.u32(6);
                std::snprintf(buf, sizeof(buf),
                    "{\"k\":\"e\",\"t\":%.3f,\"e\":\"score\",\"kind\":%d,\"pid\":%u,\"victim\":%u,\"weapon\":%d,"
                    "\"bodypart\":%u,\"weaponName\":\"%s\"}",
                    t, kind, P.u8(4), P.u8(5), (int32_t)wid, P.u8(10), name(wid).c_str());
            } else {
                std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"score\",\"kind\":%d,\"pid\":%u}",
                    t, kind, P.u8(4));
            }
            break;
        }
        case 0x28: {  // ChatFragmentEvent: first 0, no sender 1, side only 2, server 3, sender 4, total 5, piece 6, text 7
            size_t piece = P.u8(CF_PIECE);
            if (piece > 16) piece = 16;
            std::snprintf(buf, sizeof(buf),
                "{\"k\":\"e\",\"t\":%.3f,\"e\":\"chat\",\"pid\":%d,\"first\":%u,\"global\":%u,\"server\":%u,"
                "\"total\":%u,\"text\":\"%s\"}",
                t, P.u8(1) ? -1 : (int)P.u8(CF_SENDER), P.u8(0), P.u8(2), P.u8(3), P.u8(CF_TOTAL),
                esc(P.str(CF_TEXT, piece)).c_str());
            break;
        }
        case 0x3a:  // RadioMessageEvent: u16 message 0, broadcast 2, u8 pid 3
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"radio\",\"pid\":%u,\"msg\":%u,\"global\":%u}",
                t, P.u8(3), P.u16(0), P.u8(2));
            break;
        case 0x0a:  // SetVehicleEvent: u8 pid, u16 the seat's net id
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"enterVehicle\",\"pid\":%u,\"netId\":%u}",
                t, P.u8(0), P.u16(1));
            break;
        case 0x0b:  // ExitVehicleEvent: u8 pid, u8 flag (1 in the round-end teardown: not written)
            if (P.u8(1)) return false;
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"exitVehicle\",\"pid\":%u}", t, P.u8(0));
            break;
        case 0x23:  // PickupKitEvent: u8 pid, u16 the kit's net id
            if (alreadySaid(lastKit_, P.u8(0), P.u16(1))) return false;
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"pickupKit\",\"pid\":%u,\"netId\":%u}",
                t, P.u8(0), P.u16(1));
            break;
        case 0x06:  // DestroyObjectEvent: u16 net id
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"destroyObject\",\"netId\":%u}", t, P.u16(0));
            break;
        case 0x09:  // SetDefaultVehicleEvent: u8 pid, u16 the object he now controls
            if (alreadySaid(lastControl_, P.u8(0), P.u16(1))) return false;
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"control\",\"pid\":%u,\"netId\":%u}",
                t, P.u8(0), P.u16(1));
            break;
        case 0x07: {  // CreateObjectEvent: template id 0, net id 4, has-transform 6, position 7, rotation 0x13
            uint32_t tid = P.u32(0);
            float at[6] = { P.f32(7), P.f32(11), P.f32(15), P.f32(19), P.f32(23), P.f32(27) };
            bool ok = P.u8(6) != 0;
            for (float v : at) ok = ok && numOk(v);
            if (ok)
                std::snprintf(buf, sizeof(buf),
                    "{\"k\":\"e\",\"t\":%.3f,\"e\":\"createObject\",\"tid\":%u,\"netId\":%u,\"tmpl\":\"%s\","
                    "\"pos\":[%.2f,%.2f,%.2f],\"rot\":[%.2f,%.2f,%.2f]}",
                    t, tid, P.u16(4), name(tid).c_str(), at[0], at[1], at[2], at[3], at[4], at[5]);
            else
                std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"createObject\",\"tid\":%u,\"netId\":%u,\"tmpl\":\"%s\"}",
                    t, tid, P.u16(4), name(tid).c_str());
            break;
        }
        case 0x05: {  // CreateMultipleObjectsEvent (a projectile pool): template id 0, first net id 4, count 6
            uint32_t tid = P.u32(0);
            std::snprintf(buf, sizeof(buf),
                "{\"k\":\"e\",\"t\":%.3f,\"e\":\"projPool\",\"tid\":%u,\"tmpl\":\"%s\",\"netId\":%u,\"count\":%d}",
                t, tid, name(tid).c_str(), P.u16(4), P.i32(6));
            break;
        }
        case 0x36:  // SetLevelEvent: char[64] level path, char[64] game-mode file
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"setLevel\",\"level\":\"%s\",\"mode\":\"%s\"}",
                t, esc(P.str(0, 64)).c_str(), esc(P.str(64, 64)).c_str());
            break;
        case 0x24:  // GameStatusEvent: u8 status
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"gameStatus\",\"status\":%u}", t, P.u8(0));
            break;
        case 0x34:  // DataBaseCompleteEvent
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"dbComplete\"}", t);
            break;
        case 0x16: {  // GameRulesEvent: ext views 0, nose cam 1, soldier FF 2, ticket ratio 6, time limit 10, world time 14, crosshair 18
            float ff = P.f32(2), ratio = P.f32(6);
            if (!numOk(ff) || !numOk(ratio)) return false;
            std::snprintf(buf, sizeof(buf),
                "{\"k\":\"e\",\"t\":%.3f,\"e\":\"gameRules\",\"extViews\":%u,\"noseCam\":%u,\"soldierFF\":%.3f,"
                "\"ticketRatio\":%.3f,\"timeLimit\":%u,\"worldTime\":%u,\"crosshair\":%u}",
                t, P.u8(0), P.u8(1), ff, ratio, P.u32(10), P.u32(14), P.u8(18));
            break;
        }
        case 0x29:  // TimerSyncEvent: time limit 0, world time 4
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"clock\",\"timeLimit\":%u,\"worldTime\":%u}",
                t, P.u32(0), P.u32(4));
            break;
        case 0x04: {  // SimulationEvent: u8 running, f32 world time
            float world = P.f32(1);
            if (!numOk(world)) return false;
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"simStart\",\"running\":%u,\"worldTime\":%.3f}",
                t, P.u8(0), world);
            break;
        }
        case 0x1a:  // ServerInfoEvent: three {char[16], u8 length}: map id 0, mod 17, game id 34
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"serverInfo\",\"mapId\":\"%s\",\"mod\":\"%s\",\"gameId\":\"%s\"}",
                t, esc(P.str(0, 16)).c_str(), esc(P.str(17, 16)).c_str(), esc(P.str(34, 16)).c_str());
            break;
        case 0x1b:  // ServerInfoEvent2: char[32] the server's name
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"serverName\",\"name\":\"%s\"}",
                t, esc(P.str(0, 32)).c_str());
            break;
        case 0x14:  // ChallengeEvent: char[10] challenge (not written), char[16] mod 10, i32 expansion pack 27
            std::snprintf(buf, sizeof(buf), "{\"k\":\"e\",\"t\":%.3f,\"e\":\"challenge\",\"mod\":\"%s\",\"xpack\":%d}",
                t, esc(P.str(10, 16)).c_str(), P.i32(27));
            break;
        default:  // 0x27 SpecialGameEvent and the rest: not written, as on the server
            return false;
    }
    out = buf;
    return true;
}
