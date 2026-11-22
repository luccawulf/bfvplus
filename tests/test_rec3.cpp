// replay-recorder-2, items 4 and 5: the recording player's own view (cam, camv) and the action word
// his client sends each tick (in).
#include <cmath>
#include <string>
#include <vector>
#include "check.h"
#include "replay_format.h"
#include "replay_track.h"

static LiveCam camAt(float x, float y, float z, float fov, int mode, const char* tmpl, int root)
{
    LiveCam c{};
    c.have = true;
    for (int i = 0; i < 16; ++i) c.rows[i] = 0;
    c.rows[0] = c.rows[5] = c.rows[10] = c.rows[15] = 1;   // identity rotation
    c.rows[12] = x; c.rows[13] = y; c.rows[14] = z;
    c.haveFov = fov > 0;
    c.fov = fov;
    c.haveMode = mode >= 0;
    c.mode = mode;
    c.camTmpl = tmpl;
    c.root = root;
    return c;
}

TEST(cam_lines)
{
    CHECK_EQ(rf::camLine(1.5, { 10.0f, 20.5f, -3.25f, 0.0f, 0.7071f, 0.0f, 0.7071f, 1.2217f, true }),
        std::string("{\"k\":\"cam\",\"t\":1.500,\"o\":[10.00,20.50,-3.25,0.000,0.707,0.000,0.707,1.222]}"));
    CHECK_EQ(rf::camLine(1.5, { 1, 2, 3, 0, 0, 0, 1, 0, false }),
        std::string("{\"k\":\"cam\",\"t\":1.500,\"o\":[1.00,2.00,3.00,0.000,0.000,0.000,1.000,null]}"));
    CHECK_EQ(rf::camvLine(2, 0, "UH1_Camera", 812),
        std::string("{\"k\":\"camv\",\"t\":2.000,\"mode\":0,\"cam\":\"UH1_Camera\",\"root\":812}"));
}

TEST(cam_tracker_writes_on_change)
{
    CamTracker tr;
    std::vector<std::string> l;
    tr.sample(1, camAt(10, 20, 30, 1.2f, 0, "Soldier_Camera", 1048), l);
    CHECK_EQ(l.size(), (size_t)2);
    CHECK_EQ(l[0], std::string("{\"k\":\"camv\",\"t\":1.000,\"mode\":0,\"cam\":\"Soldier_Camera\",\"root\":1048}"));
    CHECK_EQ(l[1], std::string("{\"k\":\"cam\",\"t\":1.000,\"o\":[10.00,20.00,30.00,0.000,0.000,0.000,1.000,1.200]}"));
    l.clear();
    tr.sample(2, camAt(10.004f, 20, 30, 1.2f, 0, "Soldier_Camera", 1048), l);   // under 0.01 m: nothing
    CHECK_EQ(l.size(), (size_t)0);
    tr.sample(3, camAt(10.004f, 20, 30, 0.6f, 0, "Soldier_Camera", 1048), l);   // zoomed: the fov moved
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"cam\",\"t\":3.000,\"o\":[10.00,20.00,30.00,0.000,0.000,0.000,1.000,0.600]}"));
    l.clear();
    tr.sample(4, camAt(10.004f, 20, 30, 0.6f, 1, "Soldier_Camera", 1048), l);   // view mode only
    CHECK_EQ(l.size(), (size_t)1);
    CHECK_EQ(l[0], std::string("{\"k\":\"camv\",\"t\":4.000,\"mode\":1,\"cam\":\"Soldier_Camera\",\"root\":1048}"));
    l.clear();
    LiveCam none{};
    tr.sample(5, none, l);   // no view read: nothing, and what was written holds
    CHECK_EQ(l.size(), (size_t)0);
    tr.sample(6, camAt(10.004f, 20, 30, 0.6f, 1, "Soldier_Camera", 1048), l);
    CHECK_EQ(l.size(), (size_t)0);
    LiveCam bad = camAt(NAN, 20, 30, 0.6f, 1, "Soldier_Camera", 1048);
    l.clear();
    tr.sample(7, bad, l);    // a pose that isn't numbers is left out
    CHECK_EQ(l.size(), (size_t)0);
}

TEST(in_lines_and_tracker)
{
    CHECK_EQ(rf::inLine(1, { 2048, 2175, 2048, 2048, 2048, 2048 }, 0x200001u),
        std::string("{\"k\":\"in\",\"t\":1.000,\"v\":[2048,2175,2048,2048,2048,2048,2097153]}"));
    InputTracker tr;
    std::vector<std::string> l;
    LiveInput a{ true, 7, { 2048, 2048, 2048, 2048, 2048, 2048 }, 0 };
    tr.sample(1, a, l);
    CHECK_EQ(l.size(), (size_t)1);
    tr.sample(1.03, a, l);    // the same tick again: nothing
    a.serial = 8;
    tr.sample(1.06, a, l);    // a new tick with the same word: nothing
    CHECK_EQ(l.size(), (size_t)1);
    a.serial = 9;
    a.buttons = 1;            // fire
    tr.sample(1.1, a, l);
    CHECK_EQ(l.size(), (size_t)2);
    CHECK_EQ(l[1], std::string("{\"k\":\"in\",\"t\":1.100,\"v\":[2048,2048,2048,2048,2048,2048,1]}"));
    LiveInput none{};
    tr.sample(1.2, none, l);  // no action buffered (all acked): nothing
    CHECK_EQ(l.size(), (size_t)2);
}
