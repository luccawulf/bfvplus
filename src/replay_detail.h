// Stages 2-3 trackers (plan-1b): what changed since the last sample, written as BF42++ and the BFV
// server recorder write it.
// - tickets: a tk line when either side's count changes
// - control points: the long cp line on first sight, then one per team change
// - parts and engines: jn on a part's first sight (or when its address now holds another root's or
//   template's part), j when its turn relative to the root moves over 0.002 in a quaternion component,
//   g when an engine's revs or throttle move 0.01 or its flags or gear change; part ids count from 1
//   per file, shared by parts and engines
// - soldiers: st when a state, the item or the bits change, or the aim moves 1 degree; the animation
//   table once per file, before the first st
// - the recording player's hit marks: BF42++'s rule on the hit-indication timer
// - radio songs: set, play and stop events from each MusicPlayer's state
// - shots: one f line each
// Platform-independent: the Windows readers fill the Live* structs (plan-1b Tasks 9-10).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "replay_decode.h"
#include "replay_format.h"

inline constexpr size_t kAnimStatesMax = 1u << 18;   // BF42++'s MAX_ANIM_STATES

struct LiveTickets { bool valid; int team1, team2; };
class TicketTracker {
public:
    void sample(double t, const LiveTickets& now, std::vector<std::string>& lines);
    void reset() { have_ = false; }
private:
    bool have_ = false;
    int t1_ = 0, t2_ = 0;
};

struct LiveControlPoint { int id; std::string name, tmpl; bool hasPos; float x, y, z; int team; };
class CpTracker {
public:
    void sample(double t, const std::vector<LiveControlPoint>& cps, std::vector<std::string>& lines);
    void reset() { teams_.clear(); }
private:
    std::map<int, int> teams_;
};

// A child part (a RotationalBundle) and its root's absolute rows; `key` is the part object's address
// and `tmplKey` its template's, which together tell a reused address from the same part.
struct LivePart { uintptr_t key, tmplKey; int rootNetId; std::string name; float rows[16], rootRows[16]; };
struct LiveEngine { uintptr_t key, tmplKey; int rootNetId; float revs, throttle; int flags, gear; };
class PartTracker {
public:
    void sample(double t, const std::vector<LivePart>& parts, const std::vector<LiveEngine>& engines,
        std::vector<std::string>& lines);
    void reset() { slots_.clear(); nextId_ = 1; gen_ = 0; }
private:
    struct Slot {
        uintptr_t tmplKey = 0; int root = 0; int id = 0; uint64_t seen = 0;
        bool hasJ = false; float q[4] = { 0, 0, 0, 1 };
        bool hasG = false; float revs = 0, throttle = 0; int flags = 0, gear = 0;
    };
    Slot& slotFor(uintptr_t key, uintptr_t tmplKey, int root, bool& first);
    std::unordered_map<uintptr_t, Slot> slots_;
    int nextId_ = 1;
    uint64_t gen_ = 0;
};

struct LiveSoldier { int netId; SoldierFields f; };
struct AnimTable { std::vector<rf::AnimState> states; size_t total; };   // total: the game's vector size
class SoldierTracker {
public:
    bool needsAnimTable() const { return !animTried_; }
    // `table` is read only when needsAnimTable(); null when it couldn't be read (one try per file).
    void sample(double t, const std::vector<LiveSoldier>& soldiers, const AnimTable* table, std::vector<std::string>& lines);
    void reset() { last_.clear(); animTried_ = false; }
private:
    std::map<int, SoldierFields> last_;
    bool animTried_ = false;
};

class HitMarkTracker {
public:
    // v: the recording player's hit-indication timer (set to 1 on a hit, run down by the frame time).
    void sample(double t, int pid, bool have, float v, std::vector<std::string>& lines);
    void reset() { lastAt_ = -1e300; }
private:
    double lastAt_ = -1e300;
};

// Song events as the server writes them at MusicPlayer::setSongId, play and stop: a pick while playing is
// `set` (the old song's time left) then `play` (the new song's length); a stop only when something
// plays. The client's own countdown runs fast (Session D: 1.2 to 2.9 times real time) and a stopped
// radio holds the full length, so only the length at a play is the client's: the time left after it
// comes from this tracker's clock. A restart of the playing song (its time left jumps up) is a play.
struct LiveMusic { uintptr_t key; int rootNetId; bool playing; int song, group; float left; };
class MusicTracker {
public:
    void sample(double t, const std::vector<LiveMusic>& players, std::vector<std::string>& lines);
    void reset() { last_.clear(); }
private:
    struct State {
        bool playing; int song;
        double t0; float left0;   // while playing: the time left at t0
        float frozen;             // while stopped: the time left when it stopped
        float client;             // the client's value at the last sample
        float leftAt(double t) const { return playing ? (left0 - float(t - t0) > 0 ? left0 - float(t - t0) : 0.0f) : frozen; }
    };
    std::unordered_map<uintptr_t, State> last_;
};

// One round from fireBarrel: the root net id, the firing player's id (-1 none), the weapon's template
// name, its Mat4 (16 floats) and whether the recording player fired it.
struct LiveShot { int rootNetId, pid; std::string weapon; float m[16]; bool local; };
bool shotLine(double t, const LiveShot& s, std::string& out);

// fireBarrel's rounds between two of the recorder's hooks: the fire hook pushes (game thread, without
// the recorder's lock), the next event or poll drains them in order, before its own lines, so the file
// stays in time order. At most `cap` wait; more are dropped and counted (nothing drains in the menu).
struct QueuedShot { uint64_t ms; LiveShot shot; };
class ShotQueue {
public:
    explicit ShotQueue(size_t cap = 4096) : cap_(cap) {}
    void push(uint64_t ms, const LiveShot& s);
    void drain(std::vector<QueuedShot>& out);   // replaces out's contents
    void clear();                               // a new file: forget the waiting rounds and the count
    size_t dropped() const;
private:
    mutable std::mutex mu_;
    std::vector<QueuedShot> q_;
    size_t cap_, dropped_ = 0;
};
