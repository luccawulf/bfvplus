// What changed since the last sample, as BF42++ writes it:
// - an object's first sample writes `o`, and a move over 0.01 m or a quaternion change over 0.002
//   writes an `s` row; an object gone from a sample writes `d`; a net id now on another object
//   (another gid) writes `d` then a fresh `o`; a pose that fails finitePose is left out
// - with armour (stage 2), the first `o` carries maxhp and crit, and an `a` row follows the `s` line on
//   first sight and whenever the hit points move over 0.05 or the last hitter changes (hit points that
//   aren't numbers are left out)
// - the players' first sample writes `roster` (the recording player marked), then `p` rows for
//   everyone; later samples write `p` rows only for players whose row changed
// Platform-independent: replay_read.cpp fills the Live* structs from game memory.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "replay_format.h"

struct LiveObject {
    int netId; int gid; std::string tmpl; int tid; int team; float rows[16];
    // Stage 2 (plan-1b): the object's Armor component. Without it the lines are stage 1's.
    bool hasArmor = false;
    float hp = 0, maxhp = 0, crit = 0;
    int lastHit = -1;
};
struct LivePlayer { int pid, team; bool ai, local; std::string name; int ctrlNetId, rootNetId, seat; };

class ObjectTracker {
public:
    void sample(double t, const std::vector<LiveObject>& objects, std::vector<std::string>& lines);
    void reset() { known_.clear(); }

private:
    struct Known { int gid; rf::Pose pose; bool hasArmor = false; float hp = 0; int lastHit = -1; };
    std::map<int, Known> known_;
};

// ---- replay-recorder-2 -------------------------------------------------------------------------

// A round in flight: an enabled root Projectile (vtable 0x00B92294) in the object list, keyed by its
// object manager id (obj+0x48), with its template's name and place.
struct LiveProjectile { int key; std::string tmpl; float x, y, z; };

// pn on a flight's first sample, one pj line a sample with every flight whose place is numbers and
// changed since its last pj (at the written 2 decimals), and one pd line for the flights gone since
// the last sample (a pooled round flies again under its key with a new pn). A flight never written
// away from the origin gets no pj at all: the owner's Khe Sanh test (replay_20261122-021912) had 198
// binoculars' spotting markers holding still, many at exactly 0,0,0, which rewrote 12.8 MB of pj
// in 7 minutes; the server writes every sample. At most 1024 flights at once, as on the server.
class ProjectileTracker {
public:
    void sample(double t, const std::vector<LiveProjectile>& flights, std::vector<std::string>& lines);
    void reset() { flying_.clear(); }

private:
    std::map<int, std::string> flying_;   // key -> its last written place as written ("" none yet)
};

// The info line: written once the level and the player's name are known, and again whenever one of
// the four changes (a new round's level on the same connection). The server's name comes from
// ServerInfoEvent2 (0x1b: char[32]) and the level and mode from SetLevelEvent (0x36: char[64] level
// path, char[64] mode file), the same payloads replay_events.cpp writes as serverName and setLevel.
class InfoTracker {
public:
    void onEvent(int type, const uint8_t* payload, size_t n);
    void sample(double t, const std::string& player, std::vector<std::string>& lines);
    void reset() { server_.clear(); level_.clear(); mode_.clear(); written_.clear(); }

private:
    std::string server_, level_, mode_, written_;
};

// Soldiers beyond the client's animation range. BFV stops animating soldiers about 200 m from the
// player (README section 4; the distance is the lab's estimate, not read from the exe), so their st
// states go stale. A soldier is far past 200 m from the view and near again inside 190 m (so one at
// the edge doesn't flicker); a far one that leaves the sample is cleared, so his next life starts near.
// With no view (the player dead or not spawned) every soldier keeps what he was.
struct SoldierPos { int id; float x, y, z; };
class FarTracker {
public:
    static constexpr float kFar = 200.0f, kNear = 190.0f;
    void sample(double t, const float* view, const std::vector<SoldierPos>& soldiers, std::vector<std::string>& lines);
    void reset() { far_.clear(); }

private:
    std::set<int> far_;
};

// The recording player's view (item 4): the view matrix his client last drew (16 floats in the object
// rows' layout), its field of view, and his camera's view mode, template and root (see replay_read.cpp
// readCamera). `have` false when the view wasn't read; haveFov / haveMode when those were.
struct LiveCam {
    bool have = false;
    float rows[16] = {};
    bool haveFov = false;
    float fov = 0;
    bool haveMode = false;
    int mode = -1;
    std::string camTmpl;
    int root = -1;
};
// cam when the view moves over 0.01 m, a quaternion component over 0.002 or the field of view over
// 0.001 rad (an `s` row's thresholds); camv when the mode, the camera or its root changes. A sample
// with no view, or a view that isn't numbers, writes nothing and changes nothing.
class CamTracker {
public:
    void sample(double t, const LiveCam& c, std::vector<std::string>& lines);
    void reset() { haveCam_ = haveV_ = false; }

private:
    bool haveCam_ = false, haveV_ = false;
    rf::CamRow last_{};
    int mode_ = -1, root_ = -1;
    std::string tmpl_;
};

// The action word of his client's newest tick (item 5): the last entry of his BFPlayer's action
// buffer, its serial telling one tick from the next. `have` false when the buffer is empty.
struct LiveInput { bool have; uint32_t serial; uint16_t axes[6]; uint32_t buttons; };
// in when a new tick's word differs from the last written; the same word on the next tick is not repeated.
class InputTracker {
public:
    void sample(double t, const LiveInput& in, std::vector<std::string>& lines);
    void reset() { have_ = false; }

private:
    bool have_ = false;
    uint32_t serial_ = 0;
    uint16_t axes_[6] = {};
    uint32_t buttons_ = 0;
};

class PlayerTracker {
public:
    void sample(double t, const std::vector<LivePlayer>& players, std::vector<std::string>& lines);
    void reset() { rosterDone_ = false; rows_.clear(); }

private:
    bool rosterDone_ = false;
    std::map<int, rf::PlayerRow> rows_;
};
