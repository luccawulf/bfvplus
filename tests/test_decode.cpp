// Decoders for stages 2-3: raw byte blocks copied from the game into fields, at the offsets in
// bfv_addresses.h (static RE, plan-1b). These tests build the blocks themselves; Session D's raw bytes
// become fixture tests here (plan-1b Task 8), each against a value the lab server wrote.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "bfv_addresses.h"
#include "check.h"
#include "replay_decode.h"

namespace {

template <class T> void put(std::vector<uint8_t>& b, uintptr_t off, T v) { std::memcpy(b.data() + off, &v, sizeof(T)); }

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Rows (a, b, c, position) for a turn of `deg` about Y at `pos`: the client's layout, which
// poseFromBody reads (row j, component i is R[i][j]).
void yawRows(float deg, float px, float py, float pz, float r[16])
{
    const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    const float rows[16] = { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, px, py, pz, 1 };
    std::memcpy(r, rows, sizeof(rows));
}

}  // namespace

TEST(decode_armor)
{
    std::vector<uint8_t> b(0x130, 0);
    put(b, bfv::ARMOR_MAXHP, 60.0f);
    put(b, bfv::ARMOR_HP, 47.5f);
    put(b, bfv::ARMOR_CRIT, 10.0f);
    put(b, bfv::ARMOR_LASTHIT, int32_t(3));
    ArmorFields a{};
    CHECK(decodeArmor(b.data(), b.size(), a));
    CHECK(a.hp == 47.5f && a.maxhp == 60.0f && a.crit == 10.0f);
    CHECK_EQ(a.lastHit, 3);
    CHECK(!a.destroyed);
    b[bfv::ARMOR_DESTROYED] = 1;
    CHECK(decodeArmor(b.data(), b.size(), a));
    CHECK(a.destroyed);
}

TEST(decode_armor_short)
{
    std::vector<uint8_t> b(0x40, 0);
    ArmorFields a{};
    CHECK(!decodeArmor(b.data(), b.size(), a));
}

TEST(decode_engine)
{
    std::vector<uint8_t> e(0x200, 0), p(0x100, 0);
    e[bfv::ENG_RUNNING] = 1;
    e[bfv::ENG_DISABLED] = 1;
    put(e, bfv::ENG_THROTTLE, 1.0f);
    put(p, bfv::PE_REVS, 0.248f);
    put(p, bfv::PE_GEAR, int32_t(1));
    EngineFields f{};
    CHECK(decodeEngine(e.data(), e.size(), p.data(), p.size(), f));
    CHECK(near(f.revs, 0.248f) && f.throttle == 1.0f);
    CHECK_EQ(f.flags, 3);
    CHECK_EQ(f.gear, 1);
    e[bfv::ENG_DISABLED] = 0;
    CHECK(decodeEngine(e.data(), e.size(), p.data(), p.size(), f));
    CHECK_EQ(f.flags, 1);
    CHECK(!decodeEngine(e.data(), 0x100, p.data(), p.size(), f));
}

TEST(decode_tickets)
{
    std::vector<uint8_t> b(0x200, 0);
    put(b, bfv::SCORE_TEAM_BASE + 1 * bfv::SCORE_TEAM_STRIDE + bfv::SCORE_TICKETS, int32_t(600));
    put(b, bfv::SCORE_TEAM_BASE + 2 * bfv::SCORE_TEAM_STRIDE + bfv::SCORE_TICKETS, int32_t(599));
    int t1 = 0, t2 = 0;
    CHECK(decodeTickets(b.data(), b.size(), t1, t2));
    CHECK_EQ(t1, 600);
    CHECK_EQ(t2, 599);
    CHECK(!decodeTickets(b.data(), 0x80, t1, t2));
}

TEST(decode_cp_team)
{
    std::vector<uint8_t> b(0x300, 0);
    put(b, bfv::CP_TEAM, int32_t(2));
    int team = 0;
    CHECK(decodeCpTeam(b.data(), b.size(), team));
    CHECK_EQ(team, 2);
    CHECK(!decodeCpTeam(b.data(), 0x100, team));
}

TEST(decode_soldier)
{
    std::vector<uint8_t> b(0x600, 0);
    put(b, bfv::SOL_LOWER, int32_t(5));
    put(b, bfv::SOL_UPPER, int32_t(253));
    put(b, bfv::SOL_PITCH, 12.5f);
    put(b, bfv::SOL_TWIST, -3.0f);
    put(b, bfv::SOL_ITEM, int32_t(3));
    put(b, bfv::SOL_BITS, uint16_t(1));
    SoldierFields s{};
    CHECK(decodeSoldier(b.data(), b.size(), s));
    CHECK_EQ(s.lower, 5);
    CHECK_EQ(s.upper, 253);
    CHECK(s.pitch == 12.5f && s.twist == -3.0f);
    CHECK_EQ(s.item, 3);
    CHECK_EQ(s.bits, 1);
    CHECK(!decodeSoldier(b.data(), 0x300, s));
}

TEST(decode_soldier_bad_aim)
{
    std::vector<uint8_t> b(0x600, 0);
    put(b, bfv::SOL_PITCH, NAN);
    put(b, bfv::SOL_TWIST, 1000.0f);
    SoldierFields s{};
    CHECK(decodeSoldier(b.data(), b.size(), s));
    CHECK(s.pitch == 0.0f && s.twist == 0.0f);
}

TEST(part_relative_identity)
{
    float root[16], part[16], q[4], at[3];
    yawRows(0, 100, 50, 200, root);
    yawRows(0, 101, 52, 203, part);
    partRelative(root, part, q, at);
    CHECK(near(q[0], 0) && near(q[1], 0) && near(q[2], 0) && near(std::fabs(q[3]), 1));
    CHECK(near(at[0], 1) && near(at[1], 2) && near(at[2], 3));
}

TEST(part_relative_yaw)
{
    float root[16], part[16], q[4], at[3];
    yawRows(0, 0, 0, 0, root);
    yawRows(90, 0, 0, 0, part);
    partRelative(root, part, q, at);
    const float sign = q[3] < 0 ? -1.0f : 1.0f;
    CHECK(near(q[0] * sign, 0) && near(q[1] * sign, 0.7071f) && near(q[2] * sign, 0) && near(q[3] * sign, 0.7071f));
}

TEST(part_relative_rotated_root)
{
    // The same turn on root and part is no turn; the part's offset is read in the root's frame.
    float root[16], part[16], q[4], at[3];
    yawRows(90, 10, 0, 10, root);
    yawRows(90, 11, 0, 10, part);
    partRelative(root, part, q, at);
    CHECK(near(q[0], 0) && near(q[1], 0) && near(q[2], 0) && near(std::fabs(q[3]), 1));
    CHECK(near(at[0], 0) && near(at[1], 0) && near(at[2], 1));
}

TEST(shot_from_matrix)
{
    float m[16];
    yawRows(90, 993.93f, 98.62f, 1009.17f, m);
    float p[3], d[3];
    CHECK(shotFromMatrix(m, p, d));
    CHECK(p[0] == 993.93f && p[1] == 98.62f && p[2] == 1009.17f);
    CHECK(near(d[0], 1) && near(d[1], 0) && near(d[2], 0));
    m[9] = INFINITY;
    CHECK(!shotFromMatrix(m, p, d));
}

// ---- Session D fixtures (plan-1b Task 8): raw bytes the probe copied from the game, against the value the
// lab server wrote at the same moment (sd_match.py; tests/fixtures/*.txt, 2026-11-17). Each test needs a
// minimum count, so an empty or truncated file fails too.
namespace {

struct Row { std::vector<std::string> left; std::vector<double> right; std::string text; };

std::vector<Row> rows(const char* path)
{
    std::vector<Row> out;
    std::ifstream f(path);
    for (std::string l; std::getline(f, l);) {
        if (l.empty() || l[0] == '#') continue;
        const size_t bar = l.find('|');
        if (bar == std::string::npos) continue;
        Row r;
        r.text = l.substr(0, 120);
        std::istringstream a(l.substr(0, bar)), b(l.substr(bar + 1));
        for (std::string t; a >> t;) r.left.push_back(t);
        for (double v; b >> v;) r.right.push_back(v);
        out.push_back(r);
    }
    return out;
}

std::vector<uint8_t> unhex(const std::string& s)
{
    std::vector<uint8_t> b(s.size() / 2);
    for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t(std::stoi(s.substr(2 * i, 2), nullptr, 16));
    return b;
}

void floats(const Row& r, size_t from, float* out, size_t n)
{
    for (size_t i = 0; i < n; ++i) out[i] = std::stof(r.left.at(from + i));
}

// The angle between two rotations, the server's quaternion normalised first (it writes 4 decimals).
float qangle(const float a[4], const double* b)
{
    double len = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2] + b[3] * b[3]);
    double d = std::fabs((a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]) / len);
    return float(2.0 * std::acos(d > 1.0 ? 1.0 : d) * 57.29578);
}

int failures(int& bad, const Row& r, bool ok)
{
    if (!ok && ++bad <= 3) check::fail(__FILE__, __LINE__, "fixture " + r.text);
    return bad;
}

}  // namespace

TEST(armor_matches_server)
{
    auto rs = rows("tests/fixtures/armor.txt");
    int bad = 0;
    for (const Row& r : rs) {
        auto b = unhex(r.left.at(0));
        ArmorFields a{};
        bool ok = decodeArmor(b.data(), b.size(), a) && std::fabs(std::round(a.hp * 10.0f) / 10.0f - r.right[0]) < 0.051
            && std::fabs(a.maxhp - r.right[1]) < 0.05 && std::fabs(a.crit - r.right[2]) < 0.05 && a.lastHit == int(r.right[3]);
        failures(bad, r, ok);
    }
    CHECK(rs.size() >= 20);
    CHECK_EQ(bad, 0);
}

TEST(tickets_match_server)
{
    auto rs = rows("tests/fixtures/tickets.txt");
    int bad = 0;
    for (const Row& r : rs) {
        auto b = unhex(r.left.at(0));
        int t1 = 0, t2 = 0;
        failures(bad, r, decodeTickets(b.data(), b.size(), t1, t2) && t1 == int(r.right[0]) && t2 == int(r.right[1]));
    }
    CHECK(rs.size() >= 30);
    CHECK_EQ(bad, 0);
}

TEST(cp_team_matches_server)
{
    auto rs = rows("tests/fixtures/cps.txt");
    int bad = 0;
    for (const Row& r : rs) {
        auto b = unhex(r.left.at(0));
        int team = -9;
        failures(bad, r, decodeCpTeam(b.data(), b.size(), team) && team == int(r.right[0]));
    }
    CHECK(rs.size() >= 15);
    CHECK_EQ(bad, 0);
}

TEST(engine_matches_server)
{
    auto rs = rows("tests/fixtures/engines.txt");
    int bad = 0;
    for (const Row& r : rs) {
        auto e = unhex(r.left.at(0)), p = unhex(r.left.at(1));
        EngineFields f{};
        bool ok = decodeEngine(e.data(), e.size(), p.data(), p.size(), f) && std::fabs(f.revs - r.right[0]) < 0.001
            && std::fabs(f.throttle - r.right[1]) < 0.001 && f.flags == int(r.right[2]) && f.gear == int(r.right[3]);
        failures(bad, r, ok);
    }
    CHECK(rs.size() >= 15);
    CHECK_EQ(bad, 0);
}

TEST(soldier_matches_server)
{
    auto rs = rows("tests/fixtures/soldiers.txt");
    int bad = 0;
    for (const Row& r : rs) {
        auto b = unhex(r.left.at(0));
        SoldierFields s{};
        bool ok = decodeSoldier(b.data(), b.size(), s) && s.lower == int(r.right[0]) && s.upper == int(r.right[1])
            && std::fabs(s.pitch - r.right[2]) < 0.051 && std::fabs(s.twist - r.right[3]) < 0.051
            && s.item == int(r.right[4]) && s.bits == int(r.right[5]);
        failures(bad, r, ok);
    }
    CHECK(rs.size() >= 50);
    CHECK_EQ(bad, 0);
}

TEST(part_matches_server)
{
    auto rs = rows("tests/fixtures/parts.txt");
    int bad = 0;
    for (const Row& r : rs) {
        float root[16], part[16], q[4], at[3];
        floats(r, 0, root, 16);
        floats(r, 16, part, 16);
        partRelative(root, part, q, at);
        failures(bad, r, r.right.size() == 4 && qangle(q, r.right.data()) < 1.0f);
    }
    CHECK(rs.size() >= 30);
    CHECK_EQ(bad, 0);
}

TEST(shot_matches_server)
{
    auto rs = rows("tests/fixtures/fires.txt");
    int bad = 0;
    for (const Row& r : rs) {
        float m[16], p[3], d[3];
        floats(r, 0, m, 16);
        bool ok = shotFromMatrix(m, p, d) && r.right.size() == 6;
        if (ok) {
            const double dx = p[0] - r.right[0], dy = p[1] - r.right[1], dz = p[2] - r.right[2];
            const double dot = d[0] * r.right[3] + d[1] * r.right[4] + d[2] * r.right[5];
            const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
                * std::sqrt(r.right[3] * r.right[3] + r.right[4] * r.right[4] + r.right[5] * r.right[5]);
            const double c = len > 0 ? dot / len : -1.0;
            ok = std::sqrt(dx * dx + dy * dy + dz * dz) < 0.05 && std::acos(c > 1.0 ? 1.0 : c) * 57.29578 < 0.5;
        }
        failures(bad, r, ok);
    }
    CHECK(rs.size() >= 100);
    CHECK_EQ(bad, 0);
}

TEST(music_matches_server)
{
    // Playing, song and group exactly. The time left is the client's own countdown: in Session D it ran
    // 1.2 to 2.9 times faster than the server's (2 of 6 playing rows more than 3 s off) and a stopped radio
    // holds the song's full length, so the recorder takes only the length at play and counts down itself;
    // here a playing row needs a time between 0 and 1000 s.
    auto rs = rows("tests/fixtures/music.txt");
    int bad = 0, playing = 0;
    for (const Row& r : rs) {
        auto b = unhex(r.left.at(0));
        MusicFields m{};
        bool ok = decodeMusic(b.data(), b.size(), m) && int(m.playing) == int(r.right[0]) && m.song == int(r.right[1])
            && m.group == int(r.right[2]) && (!m.playing || (m.left > 0.0f && m.left < 1000.0f));
        playing += m.playing;
        failures(bad, r, ok);
    }
    CHECK(rs.size() >= 20);
    CHECK(playing >= 5);
    CHECK_EQ(bad, 0);
}

TEST(decode_music_short)
{
    std::vector<uint8_t> b(0x160, 0);
    MusicFields m{};
    CHECK(!decodeMusic(b.data(), b.size(), m));
}
