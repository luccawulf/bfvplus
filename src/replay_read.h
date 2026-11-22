// The replay recorder's reads of the live game (Windows only; offsets in bfv_addresses.h, found in
// the lab probe session). Call on the game thread: the recorder's tick is the mouse poll, which
// Session A showed runs on the same thread as the game's events.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "replay_detail.h"
#include "replay_track.h"

struct ReadStats { unsigned listSize = 0, walked = 0, soldiers = 0, vehicles = 0; };

// Stages 2-3 (plan-1b Tasks 9-10, Session D's offsets): what the same walk of the object list also finds.
// Parts are RotationalBundles and engines Engines anywhere under a sampled root, as the server recorder
// takes them; radios are MusicPlayers under a root with a net id; control points carry the client's own
// object id (the server writes its own, so the two files' ids differ: match them by name).
struct DetailRead {
    std::vector<LiveSoldier> soldiers;
    std::vector<LivePart> parts;
    std::vector<LiveEngine> engines;
    std::vector<LiveMusic> music;
    std::vector<LiveControlPoint> cps;
    std::vector<LiveProjectile> projectiles;   // replay-recorder-2: rounds in flight
};

// Every root soldier and vehicle in the object manager with a net id and a real position (pooled
// copies sit at the origin and are left out). Returns the count, or -1 when the list can't be read.
// With `detail`, each root also gets its Armor (LiveObject's stage 2 fields), and `detail` is filled.
int readObjects(std::vector<LiveObject>& out, ReadStats* stats = nullptr, DetailRead* detail = nullptr);

// Both sides' tickets from the ScoreManager (*(0x00D65748), vtable checked); false when none.
bool readTickets(LiveTickets& out);

// The recording player's hit-mark timer (BFPlayer +0x1EC), his id and his BFPlayer; false when there
// is no local player.
bool readHitMark(float& v, int& pid, uintptr_t& bfPlayer);

// The animation state table (*(0x00DE450C)): each state's index, name and flags; `total` the game's
// count, and no states read when it is over kAnimStatesMax. False when the table can't be read.
bool readAnimTable(AnimTable& out);

// A round from fireBarrel (the fire hook, game thread): the weapon's root net id and template name,
// the firing player's id (-1 none) and BFPlayer, and the 16 floats of its Mat4. False when the
// matrix can't be read.
bool readShot(uintptr_t fireArms, uintptr_t iplayer, uintptr_t mat, LiveShot& out, uintptr_t& bfPlayer);

// Every player in the player manager's list, the recording player marked `local`.
int readPlayers(std::vector<LivePlayer>& out);

bool localPlayerPresent();

// Template id -> name from the client's ObjectTemplateManager tree ("" when unknown). The tree is
// walked again when a lookup misses, at most every 5 s.
std::string templateNameById(uint32_t id);

// Forget the template id/name maps (a new file, or a level change: ids and addresses can be reused).
void templateCacheReset();
