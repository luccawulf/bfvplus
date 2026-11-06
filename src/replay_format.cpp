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
