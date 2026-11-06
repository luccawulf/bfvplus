// [replay] settings from bfvplus.ini.
#include "check.h"
#include "replay_settings.h"

TEST(record_truthy)
{
    for (const char* v : { "on", "true", "1", "yes", "ON" }) {
        ReplaySettings s;
        CHECK(applyReplaySetting(s, "recordReplays", v));
        CHECK(s.record);
    }
    ReplaySettings s;
    s.record = true;
    CHECK(applyReplaySetting(s, "RECORDREPLAYS", "off"));
    CHECK(!s.record);
}

TEST(hz_clamped)
{
    ReplaySettings s;
    applyReplaySetting(s, "sampleHz", "0");   CHECK_EQ(s.sampleHz, 1);
    applyReplaySetting(s, "sampleHz", "99");  CHECK_EQ(s.sampleHz, 30);
    applyReplaySetting(s, "sampleHz", "abc"); CHECK_EQ(s.sampleHz, 30);
    applyReplaySetting(s, "samplehz", "15");  CHECK_EQ(s.sampleHz, 15);
}

TEST(probe_flag)
{
    ReplaySettings s;
    CHECK(applyReplaySetting(s, "probe", "on"));
    CHECK(s.probe);
}

TEST(unknown_key)
{
    ReplaySettings s;
    CHECK(!applyReplaySetting(s, "colour", "on"));
    CHECK(!s.record);
    CHECK_EQ(s.sampleHz, 30);
    CHECK(!s.probe);
}
