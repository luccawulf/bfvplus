// The text of every line the replay recorder writes: BF42++'s replay format 5, as the BFV
// server recorder and the replay viewer read it. Platform-independent (no windows.h): built with
// g++ for tests/run.sh and with MSVC into dinput8.dll.
#pragma once
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace rf {

// A JSON string body (no quotes) from game text. Game strings are Latin-1: bytes 0x80 and up
// become two-byte UTF-8, control bytes become escapes.
std::string jsonEscape(std::string_view latin1);

// "replay_20261106-213005.ndjson"; dup >= 2 adds "-<dup>" for a second file in the same second.
std::string fileName(const std::tm& local, int dup);

struct Header { std::string version, mod, build, startIso; int hz; };
std::string headerLine(const Header& h);
std::string endLine(double t);

struct Pose { int id; float x, y, z, qx, qy, qz, qw; };
// Every value finite and each coordinate within 100000 m: a pooled or half-built object's
// transform fails this and is left out of the sample.
bool finitePose(const Pose& p);

std::string sLine(double t, const std::vector<Pose>& poses);
std::string oLine(double t, int id, int gid, std::string_view tmpl, int tid, int team);
std::string dLine(double t, int id);

struct PlayerRow { int pid, team, vehNetId, rootNetId, seat, triggers; };
std::string pLine(double t, const std::vector<PlayerRow>& rows);

struct RosterRow { int pid, team; bool ai; std::string name; bool local; };
std::string rosterLine(double t, const std::vector<RosterRow>& rows);

}  // namespace rf
