// What changed since the last sample, as BF42++ writes it:
// - an object's first sample writes `o`, and a move over 0.01 m or a quaternion change over 0.002
//   writes an `s` row; an object gone from a sample writes `d`; a net id now on another object
//   (another gid) writes `d` then a fresh `o`; a pose that fails finitePose is left out
// - the players' first sample writes `roster` (the recording player marked), then `p` rows for
//   everyone; later samples write `p` rows only for players whose row changed
// Platform-independent: replay_read.cpp fills the Live* structs from game memory.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "replay_format.h"

struct LiveObject { int netId; int gid; std::string tmpl; int tid; int team; float rows[16]; };
struct LivePlayer { int pid, team; bool ai, local; std::string name; int ctrlNetId, rootNetId, seat; };

class ObjectTracker {
public:
    void sample(double t, const std::vector<LiveObject>& objects, std::vector<std::string>& lines);
    void reset() { known_.clear(); }

private:
    struct Known { int gid; rf::Pose pose; };
    std::map<int, Known> known_;
};

class PlayerTracker {
public:
    void sample(double t, const std::vector<LivePlayer>& players, std::vector<std::string>& lines);
    void reset() { rosterDone_ = false; rows_.clear(); }

private:
    bool rosterDone_ = false;
    std::map<int, rf::PlayerRow> rows_;
};
