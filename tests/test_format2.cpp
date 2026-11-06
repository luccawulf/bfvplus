// Stages 2-3 record text: armour, tickets, control points, shots, hit marks, parts, engines, soldier
// states, the animation table and radio songs. Each expected line is copied from the lab server
// recorder's file (Session A) or from BF42++'s replay.cpp, so the viewer reads both alike.
#include <string>
#include <vector>
#include "check.h"
#include "replay_format.h"

TEST(o_line_armor)
{
    CHECK_EQ(rf::oLineArmor(0, 635, 4023, "F4Phantom", 3964, 0, 60.0f, 10.0f),
        std::string("{\"k\":\"o\",\"t\":0.000,\"id\":635,\"gid\":4023,\"tmpl\":\"F4Phantom\",\"tid\":3964,\"team\":0,\"maxhp\":60.0,\"crit\":10.0}"));
}

TEST(a_line)
{
    CHECK_EQ(rf::aLine(0, { { 635, 60.0f, -1 }, { 636, 100.0f, -1 } }),
        std::string("{\"k\":\"a\",\"t\":0.000,\"a\":[[635,60.0,-1],[636,100.0,-1]]}"));
}

TEST(tk_line)
{
    CHECK_EQ(rf::tkLine(26.985, 600, 599), std::string("{\"k\":\"tk\",\"t\":26.985,\"v\":[600,599]}"));
}

TEST(cp_lines)
{
    const float pos[3] = { 841.57f, 35.14f, 528.64f };
    CHECK_EQ(rf::cpFirstLine(0, 545, "OI_base1", "US_base", pos, 2),
        std::string("{\"k\":\"cp\",\"t\":0.000,\"id\":545,\"name\":\"OI_base1\",\"tmpl\":\"US_base\",\"pos\":[841.57,35.14,528.64],\"team\":2}"));
    CHECK_EQ(rf::cpFirstLine(0, 545, "OI_base1", "US_base", nullptr, -1),
        std::string("{\"k\":\"cp\",\"t\":0.000,\"id\":545,\"name\":\"OI_base1\",\"tmpl\":\"US_base\",\"team\":-1}"));
    CHECK_EQ(rf::cpLine(3, 545, 1), std::string("{\"k\":\"cp\",\"t\":3.000,\"id\":545,\"team\":1}"));
}

TEST(cp_first_line_escapes)
{
    const std::string line = rf::cpFirstLine(1, 7, "Caf\xE9 \"A\"\\B", "t\x01", nullptr, 0);
    CHECK_EQ(line, std::string("{\"k\":\"cp\",\"t\":1.000,\"id\":7,\"name\":\"Caf\xC3\xA9 \\\"A\\\"\\\\B\",\"tmpl\":\"t\\u0001\",\"team\":0}"));
}

TEST(f_line)
{
    const float p[3] = { 993.93f, 98.62f, 1009.17f }, d[3] = { 0.544f, -0.157f, 0.824f };
    CHECK_EQ(rf::fLine(58.249, 652, 252, "UH1_RocketLaunchers", p, d, false),
        std::string("{\"k\":\"f\",\"t\":58.249,\"id\":652,\"pid\":252,\"w\":\"UH1_RocketLaunchers\",\"p\":[993.93,98.62,1009.17],\"d\":[0.544,-0.157,0.824]}"));
    CHECK_EQ(rf::fLine(58.249, 652, 252, "UH1_RocketLaunchers", p, d, true),
        std::string("{\"k\":\"f\",\"t\":58.249,\"id\":652,\"pid\":252,\"w\":\"UH1_RocketLaunchers\",\"p\":[993.93,98.62,1009.17],\"d\":[0.544,-0.157,0.824],\"local\":1}"));
}

TEST(hm_line)
{
    CHECK_EQ(rf::hmLine(12.5, 3, 12.25), std::string("{\"k\":\"hm\",\"t\":12.500,\"pid\":3,\"at\":12.250}"));
}

TEST(jn_j_lines)
{
    CHECK_EQ(rf::jnLine(0, { { 652, 17, "CannonYaw", true, -2.13f, -0.83f, -0.62f } }),
        std::string("{\"k\":\"jn\",\"t\":0.000,\"o\":[[652,17,\"CannonYaw\",-2.13,-0.83,-0.62]]}"));
    CHECK_EQ(rf::jnLine(0, { { 652, 17, "CannonYaw", false, 0, 0, 0 } }),
        std::string("{\"k\":\"jn\",\"t\":0.000,\"o\":[[652,17,\"CannonYaw\"]]}"));
    CHECK_EQ(rf::jLine(0, { { 635, 1, 0.0f, -0.1131f, -0.0f, 0.9936f } }),
        std::string("{\"k\":\"j\",\"t\":0.000,\"o\":[[635,1,0.0000,-0.1131,-0.0000,0.9936]]}"));
}

TEST(g_line)
{
    CHECK_EQ(rf::gLine(32.65, { { 1087, 0.248f, 1.0f, 1, 1, 105 } }),
        std::string("{\"k\":\"g\",\"t\":32.650,\"o\":[[1087,0.248,1.000,1,1,105]]}"));
}

TEST(st_line)
{
    CHECK_EQ(rf::stLine(39.746, { { 925, 5, 253, 0.0f, 0.0f, 3, 1 } }),
        std::string("{\"k\":\"st\",\"t\":39.746,\"o\":[[925,5,253,0.0,0.0,3,1]]}"));
}

TEST(anim_lines)
{
    CHECK_EQ(rf::animLine(20.167, { { 3323, "BigExplosion", 0 }, { 2891, "Face_angry", 0 } }),
        std::string("{\"k\":\"anim\",\"t\":20.167,\"states\":[[3323,\"BigExplosion\",0],[2891,\"Face_angry\",0]]}"));
    CHECK_EQ(rf::animSkippedLine(1, 300000, 262144),
        std::string("{\"k\":\"anim\",\"t\":1.000,\"skipped\":300000,\"limit\":262144,\"states\":[]}"));
}

TEST(song_line)
{
    CHECK_EQ(rf::songLine(495.872, "play", 679, 0, 0, 135.2f),
        std::string("{\"k\":\"e\",\"t\":495.872,\"e\":\"song\",\"a\":\"play\",\"id\":679,\"song\":0,\"group\":0,\"left\":135.2}"));
}
