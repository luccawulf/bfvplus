// [replay] settings (see replay_settings.h).
#include "replay_settings.h"
#include <cctype>
#include <cstdlib>

ReplaySettings g_replay;

static std::string lower(std::string s)
{
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool applyReplaySetting(ReplaySettings& s, std::string key, std::string value)
{
    key = lower(key);
    value = lower(value);
    const bool truthy = value == "on" || value == "true" || value == "1" || value == "yes";
    if (key == "recordreplays") {
        s.record = truthy;
    } else if (key == "samplehz") {
        char* end = nullptr;
        long hz = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0') hz = 30;   // not a number: the default
        s.sampleHz = hz < 1 ? 1 : hz > 30 ? 30 : (int)hz;
    } else if (key == "probe") {
        s.probe = truthy;
    } else {
        return false;
    }
    return true;
}
