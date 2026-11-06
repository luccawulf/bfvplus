// Decoders for stages 2-3: raw byte blocks copied from the game into fields, at the offsets in
// bfv_addresses.h (static RE, plan-1b). These tests build the blocks themselves; Session D's raw bytes
// become fixture tests here (plan-1b Task 8), each against a value the lab server wrote.
#include <cmath>
#include <cstdint>
#include <cstring>
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
