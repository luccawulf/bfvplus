// replay-recorder-2: the run's labels (info), rounds in flight (pn, pj, pd) and soldiers beyond the
// client's animation range (far). The pn/pj/pd lines are copied from the lab server recorder's file
// (sessionE/server.ndjson), so the viewer and the lab tools read both alike.
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "check.h"
#include "replay_format.h"
#include "replay_track.h"

TEST(pn_pj_pd_lines_match_server)
{
    CHECK_EQ(rf::pnLine(54.514, 12235, "UH1_RocketProjectile"),
        std::string("{\"k\":\"pn\",\"t\":54.514,\"o\":[[12235,\"UH1_RocketProjectile\"]]}"));
    CHECK_EQ(rf::pjLine(54.514, { { 12235, 1130.29f, 81.18f, 1270.01f }, { 12242, 1123.95f, 81.44f, 1269.34f } }),
        std::string("{\"k\":\"pj\",\"t\":54.514,\"o\":[[12235,1130.29,81.18,1270.01],[12242,1123.95,81.44,1269.34]]}"));
    CHECK_EQ(rf::pdLine(56.683, { 12235, 12242 }), std::string("{\"k\":\"pd\",\"t\":56.683,\"o\":[12235,12242]}"));
}

TEST(projectile_flights)
{
    ProjectileTracker tr;
    std::vector<std::string> l;
    tr.sample(1, { { 7, "SA7_Projectile", 1, 2, 3 }, { 9, "Rocket", 4, 5, 6 } }, l);
    CHECK_EQ(l.size(), (size_t)3);
    CHECK_EQ(l[0], std::string("{\"k\":\"pn\",\"t\":1.000,\"o\":[[7,\"SA7_Projectile\"]]}"));
    CHECK_EQ(l[1], std::string("{\"k\":\"pn\",\"t\":1.000,\"o\":[[9,\"Rocket\"]]}"));
    CHECK_EQ(l[2], std::string("{\"k\":\"pj\",\"t\":1.000,\"o\":[[7,1.00,2.00,3.00],[9,4.00,5.00,6.00]]}"));
    l.clear();
    tr.sample(2, { { 7, "SA7_Projectile", 1.5f, 2, 3 } }, l);   // 9 has ended
    CHECK_EQ(l.size(), (size_t)2);
    CHECK_EQ(l[0], std::string("{\"k\":\"pj\",\"t\":2.000,\"o\":[[7,1.50,2.00,3.00]]}"));
    CHECK_EQ(l[1], std::string("{\"k\":\"pd\",\"t\":2.000,\"o\":[9]}"));
    l.clear();
    // The pooled 9 flies again: a new flight, named again.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    tr.sample(3, { { 7, "SA7_Projectile", nan, 2, 3 }, { 9, "Rocket", 0, 0, 0 } }, l);
    CHECK_EQ(l.size(), (size_t)2);
    CHECK_EQ(l[0], std::string("{\"k\":\"pn\",\"t\":3.000,\"o\":[[9,\"Rocket\"]]}"));
    CHECK_EQ(l[1], std::string("{\"k\":\"pj\",\"t\":3.000,\"o\":[[9,0.00,0.00,0.00]]}"));   // 7's place isn't numbers: left out, still flying
    l.clear();
    tr.sample(4, {}, l);
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"pd\",\"t\":4.000,\"o\":[7,9]}"));
    l.clear();
    tr.sample(5, {}, l);
    CHECK_EQ(l.size(), (size_t)0);
}

TEST(info_line)
{
    CHECK_EQ(rf::infoLine(0.25, "Testing \"1\"", "operation_hastings", "conquest", "Lucca"),
        std::string("{\"k\":\"info\",\"t\":0.250,\"server\":\"Testing \\\"1\\\"\",\"level\":\"operation_hastings\","
                    "\"mode\":\"conquest\",\"player\":\"Lucca\"}"));
}

static void setLevelPayload(uint8_t* b, const char* level, const char* mode)
{
    std::memset(b, 0, 128);
    std::memcpy(b, level, std::strlen(level));
    std::memcpy(b + 64, mode, std::strlen(mode));
}

TEST(info_tracker)
{
    InfoTracker info;
    std::vector<std::string> l;
    uint8_t b[128] = {};
    std::memcpy(b, "Testing", 7);
    info.onEvent(0x1b, b, 32);
    info.sample(0.1, "", l);
    CHECK_EQ(l.size(), (size_t)0);   // no level, no player yet
    setLevelPayload(b, "BfVietnam/levels/operation_hastings/", "Conquest.con");
    info.onEvent(0x36, b, 128);
    info.sample(0.2, "", l);
    CHECK_EQ(l.size(), (size_t)0);
    info.sample(9.5, "Lucca", l);
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"info\",\"t\":9.500,\"server\":\"Testing\",\"level\":\"operation_hastings\","
                               "\"mode\":\"conquest\",\"player\":\"Lucca\"}"));
    info.sample(9.6, "Lucca", l);
    CHECK_EQ(l.size(), (size_t)1);   // unchanged: nothing more
    setLevelPayload(b, "BfVietnam/levels/operation_irving/", "coop.con");
    info.onEvent(0x36, b, 128);
    info.sample(300, "Lucca", l);
    CHECK_EQ(l.size(), (size_t)2);   // the next round's level
    CHECK_EQ(l[1], std::string("{\"k\":\"info\",\"t\":300.000,\"server\":\"Testing\",\"level\":\"operation_irving\","
                               "\"mode\":\"coop\",\"player\":\"Lucca\"}"));
    info.reset();
    info.sample(301, "Lucca", l);
    CHECK_EQ(l.size(), (size_t)2);
}

TEST(far_soldiers)
{
    FarTracker ft;
    std::vector<std::string> l;
    const float view[3] = { 0, 0, 0 };
    ft.sample(1, view, { { 5, 10, 0, 0 }, { 6, 250, 0, 0 } }, l);
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"far\",\"t\":1.000,\"o\":[[6,1]]}"));
    l.clear();
    ft.sample(2, view, { { 5, 10, 0, 0 }, { 6, 195, 0, 0 } }, l);   // inside the hysteresis: still far
    CHECK_EQ(l.size(), (size_t)0);
    ft.sample(3, nullptr, { { 6, 10, 0, 0 } }, l);   // no view (the player dead): held
    CHECK_EQ(l.size(), (size_t)0);
    ft.sample(4, view, { { 5, 0, 0, 201 }, { 6, 0, 150, 0 } }, l);
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"far\",\"t\":4.000,\"o\":[[5,1],[6,0]]}"));
    l.clear();
    ft.sample(5, view, { { 6, 0, 150, 0 } }, l);   // 5 gone while far: cleared, so his next life starts near
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"far\",\"t\":5.000,\"o\":[[5,0]]}"));
    l.clear();
    ft.sample(6, view, { { 5, 0, 0, 10 } }, l);    // 5 back, near; 6 gone near: nothing to say
    CHECK_EQ(l.size(), (size_t)0);
}
