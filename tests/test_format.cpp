// Record text: every line the recorder writes, built exactly as BF42++ format 5 spells it.
#include <cmath>
#include <ctime>
#include "check.h"
#include "replay_format.h"

static std::tm tmOf(int y, int mo, int d, int h, int mi, int s)
{
    std::tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    return t;
}

TEST(escape_quote_backslash)
{
    CHECK_EQ(rf::jsonEscape("a\"b\\c"), std::string("a\\\"b\\\\c"));
}

TEST(escape_control)
{
    CHECK_EQ(rf::jsonEscape("\x01"), std::string("\\u0001"));
    CHECK_EQ(rf::jsonEscape("t\th\nx"), std::string("t\\th\\nx"));
}

TEST(escape_latin1)
{
    CHECK_EQ(rf::jsonEscape("\xE9"), std::string("\xC3\xA9"));
}

TEST(file_name)
{
    CHECK_EQ(rf::fileName(tmOf(2026, 11, 6, 21, 30, 5), 0), std::string("replay_20261106-213005.ndjson"));
    CHECK_EQ(rf::fileName(tmOf(2026, 11, 6, 21, 30, 5), 2), std::string("replay_20261106-213005-2.ndjson"));
}

TEST(header_exact)
{
    CHECK_EQ(rf::headerLine({ "0.3-dev", "bfvietnam", "Nov  6 2026 21:30:00", "2026-11-06T21:30:05", 10 }),
        std::string("{\"k\":\"h\",\"v\":5,\"plus\":\"bfvplus/0.3-dev\",\"game\":\"bfv\",\"mod\":\"bfvietnam\","
                    "\"build\":\"Nov  6 2026 21:30:00\",\"start\":\"2026-11-06T21:30:05\",\"hz\":10}"));
}

TEST(end_rounds)
{
    CHECK_EQ(rf::endLine(12.3456), std::string("{\"k\":\"end\",\"t\":12.346}"));
}

TEST(s_line)
{
    CHECK_EQ(rf::sLine(1.5, { { 600, 803.584f, 47.849f, 1581.3f, 0, 0, 0, 1 } }),
        std::string("{\"k\":\"s\",\"t\":1.500,\"o\":[[600,803.58,47.85,1581.30,0.000,0.000,0.000,1.000]]}"));
}

TEST(o_line)
{
    CHECK_EQ(rf::oLine(1.5, 682, 4001, "UAZ", 17, 1),
        std::string("{\"k\":\"o\",\"t\":1.500,\"id\":682,\"gid\":4001,\"tmpl\":\"UAZ\",\"tid\":17,\"team\":1}"));
}

TEST(d_line)
{
    CHECK_EQ(rf::dLine(2, 682), std::string("{\"k\":\"d\",\"t\":2.000,\"id\":682}"));
}

TEST(p_line)
{
    CHECK_EQ(rf::pLine(4.665, { { 0, 2, 2, 2, 0, 0 } }), std::string("{\"k\":\"p\",\"t\":4.665,\"p\":[[0,2,2,2,0,0]]}"));
}

TEST(roster_line)
{
    CHECK_EQ(rf::rosterLine(0, { { 0, 2, false, "-[ECHO]-Jeff", true } }),
        std::string("{\"k\":\"roster\",\"t\":0.000,\"p\":[[0,2,0,\"-[ECHO]-Jeff\",1]]}"));
}

TEST(pose_not_finite)
{
    rf::Pose ok{ 600, 803.584f, 47.849f, 1581.3f, 0, 0, 0, 1 };
    CHECK(rf::finitePose(ok));
    rf::Pose a = ok; a.x = NAN;      CHECK(!rf::finitePose(a));
    rf::Pose b = ok; b.qw = INFINITY; CHECK(!rf::finitePose(b));
    rf::Pose c = ok; c.z = 200000;    CHECK(!rf::finitePose(c));
}
