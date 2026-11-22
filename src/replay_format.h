// The text of every line the replay recorder writes: BF42++'s replay format 5, as the BFV
// server recorder and the replay viewer read it. Platform-independent (no windows.h): built with
// g++ for tests/run.sh and with MSVC into dinput8.dll.
#pragma once
#include <cstddef>
#include <cstdint>
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

// ---- stages 2-3 (plan-1b). Field order and decimals are the BFV server recorder's (core.c) and
//      BF42++'s, as the viewer's replay-recording.js reads them. ----------------------------------

// An object's first o when it has armour: max hit points and the critical threshold ride on it.
std::string oLineArmor(double t, int id, int gid, std::string_view tmpl, int tid, int team, float maxhp, float crit);
struct ArmorRow { int id; float hp; int lastHit; };
std::string aLine(double t, const std::vector<ArmorRow>& rows);
std::string tkLine(double t, int team1, int team2);
// A control point's first line (pos3 null: no "pos"), then one per team change.
std::string cpFirstLine(double t, int id, std::string_view name, std::string_view tmpl, const float* pos3, int team);
std::string cpLine(double t, int id, int team);
// One round leaving a weapon: the root it hangs under, the firing player (-1 none), the weapon's
// template, the muzzle and the direction; `local` for the recording player's own.
std::string fLine(double t, int rootId, int pid, std::string_view weapon, const float p[3], const float d[3], bool local);
std::string hmLine(double t, int pid, double at);
struct JointName { int root, part; std::string name; bool hasAt; float x, y, z; };
std::string jnLine(double t, const std::vector<JointName>& rows);
struct JointRow { int root, part; float qx, qy, qz, qw; };
std::string jLine(double t, const std::vector<JointRow>& rows);
struct EngineRow { int root; float revs, throttle; int flags, gear, part; };   // flags: 1 running, 2 disabled
std::string gLine(double t, const std::vector<EngineRow>& rows);
struct SoldierRow { int id, lower, upper; float pitch, twist; int item, bits; };
std::string stLine(double t, const std::vector<SoldierRow>& rows);
struct AnimState { int index; std::string name; uint32_t flags; };
std::string animLine(double t, const std::vector<AnimState>& states);
std::string animSkippedLine(double t, size_t count, size_t limit);
// A vehicle radio's song event; action "set", "play" or "stop".
std::string songLine(double t, const char* action, int rootId, int song, int group, float left);

// ---- replay-recorder-2 -------------------------------------------------------------------------

// Rounds in flight, as the BFV server recorder writes them (core.c sample_projectile and
// projectiles_gone): `pn` names a flight as it starts, `pj` places every flight once a sample, `pd`
// ends the flights a sample no longer finds. A flight's key is the object manager's id (obj+0x48),
// the server's map key: a pooled round keeps it from flight to flight.
std::string pnLine(double t, int key, std::string_view tmpl);
struct ProjRow { int key; float x, y, z; };
std::string pjLine(double t, const std::vector<ProjRow>& rows);
std::string pdLine(double t, const std::vector<int>& keys);
// The recording's labels for the website's replay list: the server's name, the level, the game mode
// ("conquest", "coop", ...) and the recording player's own name.
std::string infoLine(double t, std::string_view server, std::string_view level, std::string_view mode,
    std::string_view player);
// Soldiers beyond the client's animation range (1) or back inside it (0): their `st` states are stale
// while 1, and the viewer poses them from their movement instead.
struct FarRow { int id; bool isFar; };   // not `far`: windows.h defines it away
std::string farLine(double t, const std::vector<FarRow>& rows);
// The recording player's view as his client drew it: `cam` the place (2 decimals), the turn as an `s`
// row's quaternion (3 decimals) and the field of view in radians (3 decimals, null when not read);
// `camv` the camera's view mode, its template and the root it hangs under, on change.
struct CamRow { float x, y, z, qx, qy, qz, qw, fov; bool haveFov; };
std::string camLine(double t, const CamRow& c);
std::string camvLine(double t, int mode, std::string_view camTmpl, int root);
// The action word his client sent for a tick (PlayerAction, 0x00519DC0): the six 12-bit axes (yaw,
// pitch, roll, throttle, mouse look x, y) and the 32-bit button mask, as the server receives them.
std::string inLine(double t, const std::vector<int>& axes, uint32_t buttons);

}  // namespace rf
