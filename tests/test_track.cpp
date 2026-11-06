// Poses from the client's transform rows, and the trackers that turn each 10 Hz sample of soldiers,
// vehicles and players into o / s / d / roster / p lines (BF42++'s rules).
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "check.h"
#include "replay_pose.h"
#include "replay_track.h"

static float angleDeg(const rf::Pose& p, float qx, float qy, float qz, float qw)
{
    // The server writes 3 decimals (0.707 for 0.70711): normalise it, or rounding alone reads as ~3 degrees.
    float len = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    qx /= len; qy /= len; qz /= len; qw /= len;
    float d = std::fabs(p.qx * qx + p.qy * qy + p.qz * qz + p.qw * qw);
    if (d > 1.0f) d = 1.0f;
    return 2.0f * std::acos(d) * 57.29578f;
}

TEST(pose_matches_server)
{
    std::ifstream f("tests/fixtures/poses.txt");
    CHECK(f.good());
    int n = 0, bad = 0;
    for (std::string l; std::getline(f, l);) {
        if (l.empty() || l[0] == '#') continue;
        std::istringstream in(l.substr(0, l.find('|')));
        float rows[16];
        for (float& v : rows) in >> v;
        std::istringstream q(l.substr(l.find('|') + 1));
        float qx, qy, qz, qw;
        q >> qx >> qy >> qz >> qw;
        rf::Pose p = poseFromBody(1, rows);
        ++n;
        if (angleDeg(p, qx, qy, qz, qw) >= 1.0f && ++bad <= 3)
            check::fail(__FILE__, __LINE__, "pose " + l);
        CHECK(std::fabs(p.x - rows[12]) < 1e-4f && std::fabs(p.z - rows[14]) < 1e-4f);
    }
    CHECK(n >= 20);
    CHECK_EQ(bad, 0);
}

TEST(pose_identity)
{
    const float rows[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1 };
    rf::Pose p = poseFromBody(9, rows);
    CHECK_EQ(p.id, 9);
    CHECK(p.x == 1 && p.y == 2 && p.z == 3);
    CHECK(std::fabs(p.qx) < 1e-6f && std::fabs(p.qy) < 1e-6f && std::fabs(p.qz) < 1e-6f && std::fabs(p.qw - 1) < 1e-6f);
}

static LiveObject obj(int net, float x, int gid = 100)
{
    LiveObject o{};
    o.netId = net; o.gid = gid; o.tmpl = "UAZ"; o.tid = 17; o.team = 0;
    const float rows[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, 50, 900, 1 };
    for (int i = 0; i < 16; ++i) o.rows[i] = rows[i];
    return o;
}

static int count(const std::vector<std::string>& v, const char* kind)
{
    int n = 0;
    for (auto& l : v) n += l.rfind(std::string("{\"k\":\"") + kind + "\"", 0) == 0;
    return n;
}

TEST(tracker_first_seen)
{
    ObjectTracker tr;
    std::vector<std::string> lines;
    tr.sample(1.0, { obj(600, 800) }, lines);
    CHECK_EQ(lines.size(), size_t(2));
    CHECK_EQ(lines[0], std::string("{\"k\":\"o\",\"t\":1.000,\"id\":600,\"gid\":100,\"tmpl\":\"UAZ\",\"tid\":17,\"team\":0}"));
    CHECK_EQ(lines[1], std::string("{\"k\":\"s\",\"t\":1.000,\"o\":[[600,800.00,50.00,900.00,0.000,0.000,0.000,1.000]]}"));
}

TEST(tracker_change_thresholds)
{
    ObjectTracker tr;
    std::vector<std::string> lines;
    tr.sample(1.0, { obj(600, 800) }, lines);
    lines.clear();
    tr.sample(1.1, { obj(600, 800.005f) }, lines);
    CHECK_EQ(count(lines, "s"), 0);
    tr.sample(1.2, { obj(600, 800.02f) }, lines);
    CHECK_EQ(count(lines, "s"), 1);
    lines.clear();
    LiveObject turned = obj(600, 800.02f);
    // A small yaw: the quaternion's y component moves by about 0.003.
    float a = 0.006f;
    turned.rows[0] = std::cos(a); turned.rows[2] = -std::sin(a); turned.rows[8] = std::sin(a); turned.rows[10] = std::cos(a);
    tr.sample(1.3, { turned }, lines);
    CHECK_EQ(count(lines, "s"), 1);
}

TEST(tracker_vanished)
{
    ObjectTracker tr;
    std::vector<std::string> lines;
    tr.sample(1.0, { obj(600, 800), obj(601, 700) }, lines);
    lines.clear();
    tr.sample(1.1, { obj(600, 800) }, lines);
    CHECK_EQ(lines.size(), size_t(1));
    CHECK_EQ(lines[0], std::string("{\"k\":\"d\",\"t\":1.100,\"id\":601}"));
    lines.clear();
    tr.sample(1.2, { obj(600, 800) }, lines);
    CHECK(lines.empty());
}

TEST(tracker_id_reused)
{
    // The same net id on a different object (another gid) between two samples: d, then a fresh o.
    ObjectTracker tr;
    std::vector<std::string> lines;
    tr.sample(1.0, { obj(600, 800, 100) }, lines);
    lines.clear();
    tr.sample(1.1, { obj(600, 800, 222) }, lines);
    CHECK_EQ(count(lines, "d"), 1);
    CHECK_EQ(count(lines, "o"), 1);
    CHECK(lines[0].rfind("{\"k\":\"d\"", 0) == 0);
}

TEST(tracker_skips_bad_pose)
{
    ObjectTracker tr;
    std::vector<std::string> lines;
    LiveObject bad = obj(602, 0);
    bad.rows[12] = NAN;
    tr.sample(1.0, { obj(600, 800), bad }, lines);
    for (auto& l : lines) CHECK(l.find("nan") == std::string::npos && l.find("602") == std::string::npos);
}

static LivePlayer pl(int pid, int team, bool local = false)
{
    LivePlayer p{};
    p.pid = pid; p.team = team; p.ai = !local; p.local = local; p.name = local ? "me" : "bot";
    p.ctrlNetId = 700 + pid; p.rootNetId = 700 + pid; p.seat = 0;
    return p;
}

TEST(players_roster_then_p)
{
    PlayerTracker tr;
    std::vector<std::string> lines;
    tr.sample(0.5, { pl(0, 2, true), pl(1, 1) }, lines);
    CHECK_EQ(lines.size(), size_t(2));
    CHECK_EQ(lines[0], std::string("{\"k\":\"roster\",\"t\":0.500,\"p\":[[0,2,0,\"me\",1],[1,1,1,\"bot\",0]]}"));
    CHECK_EQ(lines[1], std::string("{\"k\":\"p\",\"t\":0.500,\"p\":[[0,2,700,700,0,0],[1,1,701,701,0,0]]}"));
    lines.clear();
    tr.sample(0.6, { pl(0, 2, true), pl(1, 1) }, lines);
    CHECK(lines.empty());
    tr.sample(0.7, { pl(0, 1, true), pl(1, 1) }, lines);
    CHECK_EQ(lines.size(), size_t(1));
    CHECK_EQ(lines[0], std::string("{\"k\":\"p\",\"t\":0.700,\"p\":[[0,1,700,700,0,0]]}"));
}

TEST(players_reset_writes_roster_again)
{
    PlayerTracker tr;
    std::vector<std::string> lines;
    tr.sample(0.5, { pl(0, 2, true) }, lines);
    tr.reset();
    lines.clear();
    tr.sample(0.0, { pl(0, 2, true) }, lines);
    CHECK_EQ(count(lines, "roster"), 1);
    CHECK_EQ(count(lines, "p"), 1);
}
