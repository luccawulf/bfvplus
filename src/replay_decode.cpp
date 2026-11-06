// Stages 2-3 decoders (see replay_decode.h).
#include "replay_decode.h"
#include <cmath>
#include <cstring>
#include "bfv_addresses.h"
#include "replay_pose.h"

namespace {

template <class T> T at(const uint8_t* b, uintptr_t off)
{
    T v;
    std::memcpy(&v, b + off, sizeof(T));
    return v;
}

float aim(float v) { return v > -360.0f && v < 360.0f ? v : 0.0f; }

}  // namespace

bool decodeArmor(const uint8_t* b, size_t n, ArmorFields& out)
{
    if (!b || n < bfv::ARMOR_DESTROYED + 1) return false;
    out.hp = at<float>(b, bfv::ARMOR_HP);
    out.maxhp = at<float>(b, bfv::ARMOR_MAXHP);
    out.crit = at<float>(b, bfv::ARMOR_CRIT);
    out.lastHit = at<int32_t>(b, bfv::ARMOR_LASTHIT);
    out.destroyed = b[bfv::ARMOR_DESTROYED] != 0;
    return true;
}

bool decodeEngine(const uint8_t* e, size_t ne, const uint8_t* p, size_t np, EngineFields& out)
{
    if (!e || !p || ne < bfv::ENG_RUNNING + 2 || ne < bfv::ENG_THROTTLE + 4 || np < bfv::PE_GEAR + 4) return false;
    out.revs = at<float>(p, bfv::PE_REVS);
    out.gear = at<int32_t>(p, bfv::PE_GEAR);
    out.throttle = at<float>(e, bfv::ENG_THROTTLE);
    out.flags = (e[bfv::ENG_RUNNING] ? 1 : 0) | (e[bfv::ENG_DISABLED] ? 2 : 0);
    return true;
}

bool decodeTickets(const uint8_t* b, size_t n, int& team1, int& team2)
{
    const uintptr_t t1 = bfv::SCORE_TEAM_BASE + 1 * bfv::SCORE_TEAM_STRIDE + bfv::SCORE_TICKETS;
    const uintptr_t t2 = bfv::SCORE_TEAM_BASE + 2 * bfv::SCORE_TEAM_STRIDE + bfv::SCORE_TICKETS;
    if (!b || n < t2 + 4) return false;
    team1 = at<int32_t>(b, t1);
    team2 = at<int32_t>(b, t2);
    return true;
}

bool decodeCpTeam(const uint8_t* b, size_t n, int& team)
{
    if (!b || n < bfv::CP_TEAM + 4) return false;
    team = at<int32_t>(b, bfv::CP_TEAM);
    return true;
}

bool decodeSoldier(const uint8_t* b, size_t n, SoldierFields& out)
{
    const uintptr_t need[] = { bfv::SOL_LOWER + 4, bfv::SOL_UPPER + 4, bfv::SOL_PITCH + 4, bfv::SOL_TWIST + 4,
        bfv::SOL_ITEM + 4, bfv::SOL_BITS + 2 };
    if (!b) return false;
    for (uintptr_t k : need)
        if (n < k) return false;
    out.lower = at<int32_t>(b, bfv::SOL_LOWER);
    out.upper = at<int32_t>(b, bfv::SOL_UPPER);
    out.pitch = aim(at<float>(b, bfv::SOL_PITCH));
    out.twist = aim(at<float>(b, bfv::SOL_TWIST));
    out.item = at<int32_t>(b, bfv::SOL_ITEM);
    out.bits = at<uint16_t>(b, bfv::SOL_BITS);
    return true;
}

void partRelative(const float rootRows[16], const float partRows[16], float q[4], float pos[3])
{
    const rf::Pose r = poseFromBody(0, rootRows), p = poseFromBody(0, partRows);
    // conj(r) * p
    const float ax = -r.qx, ay = -r.qy, az = -r.qz, aw = r.qw;
    q[0] = aw * p.qx + ax * p.qw + ay * p.qz - az * p.qy;
    q[1] = aw * p.qy - ax * p.qz + ay * p.qw + az * p.qx;
    q[2] = aw * p.qz + ax * p.qy - ay * p.qx + az * p.qw;
    q[3] = aw * p.qw - ax * p.qx - ay * p.qy - az * p.qz;
    const float dx = partRows[12] - rootRows[12], dy = partRows[13] - rootRows[13], dz = partRows[14] - rootRows[14];
    for (int k = 0; k < 3; ++k)
        pos[k] = dx * rootRows[4 * k] + dy * rootRows[4 * k + 1] + dz * rootRows[4 * k + 2];
}

bool shotFromMatrix(const float m[16], float p[3], float d[3])
{
    for (int k = 0; k < 3; ++k) {
        p[k] = m[12 + k];
        d[k] = m[8 + k];
        if (!std::isfinite(p[k]) || !std::isfinite(d[k]) || std::fabs(p[k]) > 100000.0f) return false;
    }
    return true;
}
