// Record text for the replay recorder (see replay_format.h). Numbers are written with snprintf so
// MSVC and g++ produce the same bytes: times and quaternions to 3 decimals, positions to 2, as
// BF42++'s replay.cpp writes them.
#include "replay_format.h"
#include <cmath>
#include <cstdio>

namespace rf {

namespace {

std::string fmt(const char* f, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

std::string t3(double t) { return fmt("%.3f", t); }

}  // namespace

std::string jsonEscape(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else if (c >= 0x80) {
                    out += (char)(0xC0 | (c >> 6));
                    out += (char)(0x80 | (c & 0x3F));
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

std::string fileName(const std::tm& t, int dup)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "replay_%04d%02d%02d-%02d%02d%02d",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    std::string name = buf;
    if (dup >= 2) name += "-" + std::to_string(dup);
    return name + ".ndjson";
}

std::string headerLine(const Header& h)
{
    return "{\"k\":\"h\",\"v\":5,\"plus\":\"bfvplus/" + jsonEscape(h.version) + "\",\"game\":\"bfv\",\"mod\":\""
        + jsonEscape(h.mod) + "\",\"build\":\"" + jsonEscape(h.build) + "\",\"start\":\"" + jsonEscape(h.startIso)
        + "\",\"hz\":" + std::to_string(h.hz) + "}";
}

std::string endLine(double t)
{
    return "{\"k\":\"end\",\"t\":" + t3(t) + "}";
}

bool finitePose(const Pose& p)
{
    const float v[] = { p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw };
    for (float f : v)
        if (!std::isfinite(f)) return false;
    return std::fabs(p.x) <= 100000.0f && std::fabs(p.y) <= 100000.0f && std::fabs(p.z) <= 100000.0f;
}

std::string sLine(double t, const std::vector<Pose>& poses)
{
    std::string out = "{\"k\":\"s\",\"t\":" + t3(t) + ",\"o\":[";
    char buf[192];
    for (size_t i = 0; i < poses.size(); ++i) {
        const Pose& p = poses[i];
        std::snprintf(buf, sizeof(buf), "%s[%d,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f]", i ? "," : "",
            p.id, p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw);
        out += buf;
    }
    return out + "]}";
}

std::string oLine(double t, int id, int gid, std::string_view tmpl, int tid, int team)
{
    return "{\"k\":\"o\",\"t\":" + t3(t) + ",\"id\":" + std::to_string(id) + ",\"gid\":" + std::to_string(gid)
        + ",\"tmpl\":\"" + jsonEscape(tmpl) + "\",\"tid\":" + std::to_string(tid) + ",\"team\":" + std::to_string(team)
        + "}";
}

std::string dLine(double t, int id)
{
    return "{\"k\":\"d\",\"t\":" + t3(t) + ",\"id\":" + std::to_string(id) + "}";
}

std::string pLine(double t, const std::vector<PlayerRow>& rows)
{
    std::string out = "{\"k\":\"p\",\"t\":" + t3(t) + ",\"p\":[";
    char buf[128];
    for (size_t i = 0; i < rows.size(); ++i) {
        const PlayerRow& r = rows[i];
        std::snprintf(buf, sizeof(buf), "%s[%d,%d,%d,%d,%d,%d]", i ? "," : "",
            r.pid, r.team, r.vehNetId, r.rootNetId, r.seat, r.triggers);
        out += buf;
    }
    return out + "]}";
}

// ---- stages 2-3 ----------------------------------------------------------------------------

namespace {

// "[%d,..." entries joined into one {"k":<kind>,"t":..,"<arr>":[...]} line.
template <class Row, class Fn>
std::string batch(const char* kind, const char* arr, double t, const std::vector<Row>& rows, Fn entry)
{
    std::string out = std::string("{\"k\":\"") + kind + "\",\"t\":" + t3(t) + ",\"" + arr + "\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) out += ",";
        out += entry(rows[i]);
    }
    return out + "]}";
}

std::string f3(const char* f, float a, float b, float c)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

}  // namespace

std::string oLineArmor(double t, int id, int gid, std::string_view tmpl, int tid, int team, float maxhp, float crit)
{
    std::string o = oLine(t, id, gid, tmpl, tid, team);
    o.pop_back();   // the closing brace
    char buf[64];
    std::snprintf(buf, sizeof(buf), ",\"maxhp\":%.1f,\"crit\":%.1f}", maxhp, crit);
    return o + buf;
}

std::string aLine(double t, const std::vector<ArmorRow>& rows)
{
    return batch("a", "a", t, rows, [](const ArmorRow& r) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[%d,%.1f,%d]", r.id, r.hp, r.lastHit);
        return std::string(buf);
    });
}

std::string tkLine(double t, int team1, int team2)
{
    return "{\"k\":\"tk\",\"t\":" + t3(t) + ",\"v\":[" + std::to_string(team1) + "," + std::to_string(team2) + "]}";
}

std::string cpFirstLine(double t, int id, std::string_view name, std::string_view tmpl, const float* pos3, int team)
{
    std::string out = "{\"k\":\"cp\",\"t\":" + t3(t) + ",\"id\":" + std::to_string(id) + ",\"name\":\"" + jsonEscape(name)
        + "\",\"tmpl\":\"" + jsonEscape(tmpl) + "\"";
    if (pos3) out += ",\"pos\":" + f3("[%.2f,%.2f,%.2f]", pos3[0], pos3[1], pos3[2]);
    return out + ",\"team\":" + std::to_string(team) + "}";
}

std::string cpLine(double t, int id, int team)
{
    return "{\"k\":\"cp\",\"t\":" + t3(t) + ",\"id\":" + std::to_string(id) + ",\"team\":" + std::to_string(team) + "}";
}

std::string fLine(double t, int rootId, int pid, std::string_view weapon, const float p[3], const float d[3], bool local)
{
    return "{\"k\":\"f\",\"t\":" + t3(t) + ",\"id\":" + std::to_string(rootId) + ",\"pid\":" + std::to_string(pid)
        + ",\"w\":\"" + jsonEscape(weapon) + "\",\"p\":" + f3("[%.2f,%.2f,%.2f]", p[0], p[1], p[2])
        + ",\"d\":" + f3("[%.3f,%.3f,%.3f]", d[0], d[1], d[2]) + (local ? ",\"local\":1}" : "}");
}

std::string hmLine(double t, int pid, double at)
{
    return "{\"k\":\"hm\",\"t\":" + t3(t) + ",\"pid\":" + std::to_string(pid) + ",\"at\":" + t3(at) + "}";
}

std::string jnLine(double t, const std::vector<JointName>& rows)
{
    return batch("jn", "o", t, rows, [](const JointName& r) {
        std::string e = "[" + std::to_string(r.root) + "," + std::to_string(r.part) + ",\"" + jsonEscape(r.name) + "\"";
        if (r.hasAt) e += f3(",%.2f,%.2f,%.2f", r.x, r.y, r.z);
        return e + "]";
    });
}

std::string jLine(double t, const std::vector<JointRow>& rows)
{
    return batch("j", "o", t, rows, [](const JointRow& r) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[%d,%d,%.4f,%.4f,%.4f,%.4f]", r.root, r.part, r.qx, r.qy, r.qz, r.qw);
        return std::string(buf);
    });
}

std::string gLine(double t, const std::vector<EngineRow>& rows)
{
    return batch("g", "o", t, rows, [](const EngineRow& r) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[%d,%.3f,%.3f,%d,%d,%d]", r.root, r.revs, r.throttle, r.flags, r.gear, r.part);
        return std::string(buf);
    });
}

std::string stLine(double t, const std::vector<SoldierRow>& rows)
{
    return batch("st", "o", t, rows, [](const SoldierRow& r) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[%d,%d,%d,%.1f,%.1f,%d,%d]", r.id, r.lower, r.upper, r.pitch, r.twist, r.item, r.bits);
        return std::string(buf);
    });
}

std::string animLine(double t, const std::vector<AnimState>& states)
{
    return batch("anim", "states", t, states, [](const AnimState& s) {
        return "[" + std::to_string(s.index) + ",\"" + jsonEscape(s.name) + "\"," + std::to_string(s.flags) + "]";
    });
}

std::string animSkippedLine(double t, size_t count, size_t limit)
{
    return "{\"k\":\"anim\",\"t\":" + t3(t) + ",\"skipped\":" + std::to_string(count) + ",\"limit\":" + std::to_string(limit)
        + ",\"states\":[]}";
}

std::string songLine(double t, const char* action, int rootId, int song, int group, float left)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", left);
    return "{\"k\":\"e\",\"t\":" + t3(t) + ",\"e\":\"song\",\"a\":\"" + action + "\",\"id\":" + std::to_string(rootId)
        + ",\"song\":" + std::to_string(song) + ",\"group\":" + std::to_string(group) + ",\"left\":" + buf + "}";
}

std::string rosterLine(double t, const std::vector<RosterRow>& rows)
{
    std::string out = "{\"k\":\"roster\",\"t\":" + t3(t) + ",\"p\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        const RosterRow& r = rows[i];
        if (i) out += ",";
        out += "[" + std::to_string(r.pid) + "," + std::to_string(r.team) + "," + (r.ai ? "1" : "0") + ",\""
            + jsonEscape(r.name) + "\"," + (r.local ? "1" : "0") + "]";
    }
    return out + "]}";
}

}  // namespace rf
