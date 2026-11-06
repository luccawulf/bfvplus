// The replay recorder's reads of the live game (Windows only; offsets in bfv_addresses.h, found in
// the lab probe session). Call on the game thread: the recorder's tick is the mouse poll, which
// Session A showed runs on the same thread as the game's events.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "replay_track.h"

struct ReadStats { unsigned listSize = 0, walked = 0, soldiers = 0, vehicles = 0; };

// Every root soldier and vehicle in the object manager with a net id and a real position (pooled
// copies sit at the origin and are left out). Returns the count, or -1 when the list can't be read.
int readObjects(std::vector<LiveObject>& out, ReadStats* stats = nullptr);

// Every player in the player manager's list, the recording player marked `local`.
int readPlayers(std::vector<LivePlayer>& out);

bool localPlayerPresent();

// Template id -> name from the client's ObjectTemplateManager tree ("" when unknown). The tree is
// walked again when a lookup misses, at most every 5 s.
std::string templateNameById(uint32_t id);
