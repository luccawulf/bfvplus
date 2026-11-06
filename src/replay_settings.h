// [replay] settings from bfvplus.ini. Platform-independent (no windows.h).
//
//   [replay]
//   recordReplays = off   ; on: write replays\replay_<time>.ndjson while connected
//   sampleHz      = 30    ; object samples per second, 1-30 (30: the server recorder's rate)
//   probe         = off   ; developer switch: write logs\bfvplus_probe.log (address finding)
#pragma once
#include <string>

struct ReplaySettings {
    bool record = false;
    int sampleHz = 30;
    bool probe = false;
};

// Apply one `key = value` from the [replay] section. Keys and values are case-insensitive.
// Returns false (and changes nothing) for an unknown key.
bool applyReplaySetting(ReplaySettings& s, std::string key, std::string value);

extern ReplaySettings g_replay;
