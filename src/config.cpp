// Minimal INI parser for bfvplus.ini.
//
// Format:
//   [presetcolors]
//   color = #00ff00
//   color = #ffff00
//   color = #ff0000
//   color = #00aaff
//
// The order of entries determines cycle order. Comments start with ; or #.
// Whitespace and blank lines are ignored.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"
#include "buddycolors.h"
#include "capture.h"
#include "keyhash.h"
#include "sensitivity.h"
#include "replay_settings.h"

#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>

// Declared in buddycolors.cpp.
void setPresetColors(const std::vector<uint32_t>& colors);

static void trim(std::string& s)
{
    size_t start = 0;
    while (start < s.size() && (unsigned char)s[start] <= ' ') start++;
    size_t end = s.size();
    while (end > start && (unsigned char)s[end - 1] <= ' ') end--;
    if (start > 0 || end < s.size()) s = s.substr(start, end - start);
}

// Parse "#RRGGBB" or "#RGB" -> 24-bit 0xRRGGBB. Returns false on bad input.
static bool parseHexColor(const std::string& s, uint32_t& out)
{
    if (s.size() < 2 || s[0] != '#') return false;
    std::string hex = s.substr(1);

    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (hex.size() == 6) {
        int v[6];
        for (int i = 0; i < 6; ++i) {
            v[i] = hexval(hex[i]);
            if (v[i] < 0) return false;
        }
        int r = (v[0] << 4) | v[1];
        int g = (v[2] << 4) | v[3];
        int b = (v[4] << 4) | v[5];
        out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return true;
    }
    if (hex.size() == 3) {
        int r = hexval(hex[0]); if (r < 0) return false;
        int g = hexval(hex[1]); if (g < 0) return false;
        int b = hexval(hex[2]); if (b < 0) return false;
        r = (r << 4) | r;
        g = (g << 4) | g;
        b = (b << 4) | b;
        out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return true;
    }
    return false;
}

// Build the absolute path to bfvplus.ini next to BfVietnam.exe.
static std::string getIniPath()
{
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "bfvplus.ini";

    for (DWORD i = len; i > 0; --i) {
        if (path[i - 1] == '\\' || path[i - 1] == '/') {
            path[i] = '\0';
            break;
        }
    }

    std::string result(path);
    result += "bfvplus.ini";
    return result;
}

// Write a default INI explaining how the cycle works.
static void writeDefaultIni(const std::string& path)
{
    std::ofstream f(path);
    if (!f) return;
    f << "; bfvplus configuration\n";
    f << ";\n";
    f << "; The Add Buddy button on the scoreboard cycles through the colors\n";
    f << "; below. First click adds the player as a buddy (default green).\n";
    f << "; Each subsequent click advances to the next color in this list.\n";
    f << "; Hold Shift while clicking to assign a random bright color instead.\n";
    f << ";\n";
    f << "; Add or remove `color = #RRGGBB` lines to customize the cycle.\n";
    f << "; Order matters - first entry is what comes after the last.\n";
    f << "\n";
    f << "[presetcolors]\n";
    f << "color = #00ff00   ; green (BFV default)\n";
    f << "color = #ffff00   ; yellow\n";
    f << "color = #ff0000   ; red\n";
    f << "color = #00aaff   ; light blue\n";
    f << "\n";
    f << "[capture]\n";
    f << "; Log every GameEvent the server sends to logs/bfvplus_server.log as JSON\n";
    f << "; Lines (one event per line: type, id, sequence, raw bytes). This is the\n";
    f << "; SSM (server-side mod) capture stream. on = enabled, off = disabled.\n";
    f << "enabled = on\n";
    f << ";\n";
    f << "; dumpObjects: snapshot the live vehicles/soldiers and their template names\n";
    f << "; to the same log (BfvObject lines). Written on join and then every\n";
    f << "; dumpObjectsInterval seconds. on = enabled, off = disabled.\n";
    f << "dumpObjects = on\n";
    f << "; Seconds between object snapshots (0 = only snapshot on join).\n";
    f << "dumpObjectsInterval = 30\n";
    f << "\n";
#ifndef BFVPLUS_PUBLIC
    f << "[keyhash]\n";
    f << "; Present a chosen 32-hex CD-key hash to the server instead of your real one,\n";
    f << "; so you can test your own (unpure) server with a known identity. Uncomment and\n";
    f << "; set a 32-character hex value below, or drop a keyhash.txt (first 32 hex chars)\n";
    f << "; next to BfVietnam.exe. Leave commented out to keep your real hash.\n";
    f << ";override = <32 hex characters>\n";
    f << "\n";
#endif
    f << "[sensitivity]\n";
    f << "; Correct the look sensitivity that feels doubled while moving on foot.\n";
    f << "; BFV scales the yaw delta by a per-axis modifier that the pitch delta\n";
    f << "; never receives, so horizontal ends up faster than vertical. This scales\n";
    f << "; the mouse X axis to compensate. Scaling happens at the DirectInput layer,\n";
    f << "; so the game and the server see the same input and there is no desync.\n";
    f << "; NOTE: this scales real mouse input, so it affects aim, not just view.\n";
    f << "correctedLookSensitivity = off\n";
    f << "; Multiplier applied while the trigger holds. bf42plus uses 0.5.\n";
    f << "correctedLookScale = 0.5\n";
    f << "; What counts as moving: strafe (default) or any (forward or strafe,\n";
    f << "; which is what bf42plus does on BF1942).\n";
    f << "correctedLookTrigger = strafe\n";
    f << ";\n";
    f << "; Use your Infantry mouse sensitivity instead of LandSea while on a\n";
    f << "; stationary MG, so it does not change feel when you mount one.\n";
    f << "stationaryMGInfSensitivity = off\n";
    f << "\n";
    f << "[replay]\n";
    f << "; Record each game you join to replays\\replay_<time>.ndjson (BF42++'s format)\n";
    f << "; for the replay viewer. off = disabled (default), on = enabled.\n";
    f << "recordReplays = off\n";
    f << "; Object samples per second, 1-30. 30 matches the server replay recorder;\n";
    f << "; the game polls its input faster than that, so 30 is reached in practice.\n";
    f << "sampleHz = 30\n";
    f.close();
}

void loadConfig()
{
    std::string iniPath = getIniPath();

    DWORD attrs = GetFileAttributesA(iniPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        writeDefaultIni(iniPath);
        return;  // built-in defaults are already in effect
    }

    std::ifstream f(iniPath);
    if (!f) return;

    std::vector<uint32_t> colors;
    std::string line, section;

    while (std::getline(f, line)) {
        trim(line);
        if (line.empty()) continue;
        if (line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            trim(section);
            for (auto& c : section) c = (char)tolower((unsigned char)c);
            continue;
        }

        // parse `key = value` (strip inline comments) for any section
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);

        // Strip inline comments from value (everything after ; or #).
        size_t cmt = value.find_first_of(";#");
        if (cmt != std::string::npos) {
            value = value.substr(0, cmt);
            trim(value);
        }

        if (key.empty() || value.empty()) continue;

        if (section == "capture") {
            std::string k = key, v = value;
            for (auto& ch : k) ch = (char)tolower((unsigned char)ch);
            for (auto& ch : v) ch = (char)tolower((unsigned char)ch);
            bool truthy = (v == "on" || v == "true" || v == "1" || v == "yes");
            if (k == "enabled") {
                g_captureEnabled = truthy;
            } else if (k == "dumpobjects") {
                g_dumpObjects = truthy;
            } else if (k == "dumpobjectsinterval") {
                g_dumpObjectsInterval = atoi(value.c_str());
            }
            continue;
        }

#ifndef BFVPLUS_PUBLIC
        if (section == "keyhash") {
            std::string k = key;
            for (auto& ch : k) ch = (char)tolower((unsigned char)ch);
            if (k == "override") setKeyhashOverride(value.c_str());
            continue;
        }
#endif

        if (section == "replay") {
            applyReplaySetting(g_replay, key, value);
            continue;
        }

        if (section == "sensitivity") {
            std::string k = key, v = value;
            for (auto& ch : k) ch = (char)tolower((unsigned char)ch);
            for (auto& ch : v) ch = (char)tolower((unsigned char)ch);
            bool truthy = (v == "on" || v == "true" || v == "1" || v == "yes");
            if (k == "correctedlooksensitivity") {
                g_strafeFixEnabled = truthy;
            } else if (k == "correctedlookscale") {
                g_strafeFixScale = (float)atof(value.c_str());
            } else if (k == "correctedlooktrigger") {
                g_strafeFixAnyMove = (v == "any");
            } else if (k == "stationarymginfsensitivity") {
                g_mgInfSensEnabled = truthy;
            }
            continue;
        }

        if (section != "presetcolors") continue;

        uint32_t c;
        if (parseHexColor(value, c)) colors.push_back(c);
    }

    setPresetColors(colors);
}
