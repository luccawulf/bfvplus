// Stages 2-3 fields from raw byte blocks copied out of the game: an Armor component, an Engine and its
// PhysicsEngine, the ScoreManager, a ControlPoint and a BFSoldier, at the offsets in bfv_addresses.h.
// The Windows side only copies the blocks (SEH-guarded); everything here is testable with g++, and
// Session D's raw bytes become fixtures against the values the lab server wrote. Each decoder returns
// false when the block is too short for its offsets. Platform-independent (no windows.h).
#pragma once
#include <cstddef>
#include <cstdint>

struct ArmorFields { float hp, maxhp, crit; int lastHit; bool destroyed; };
bool decodeArmor(const uint8_t* armor, size_t n, ArmorFields& out);

struct EngineFields { float revs, throttle; int flags, gear; };   // flags: 1 running, 2 disabled
bool decodeEngine(const uint8_t* eng, size_t ne, const uint8_t* phys, size_t np, EngineFields& out);

bool decodeTickets(const uint8_t* scoreManager, size_t n, int& team1, int& team2);

bool decodeCpTeam(const uint8_t* cp, size_t n, int& team);

// A pitch or twist outside +-360 degrees (or not a number) reads 0, as the server recorder does.
struct SoldierFields { int lower, upper; float pitch, twist; int item, bits; };
bool decodeSoldier(const uint8_t* soldier, size_t n, SoldierFields& out);

// A part's turn relative to its root (conj(q_root) * q_part, poseFromBody's quaternions) and where it
// sits in the root's frame (its offset projected on the root's rows a, b, c): BF42++'s and the server's.
void partRelative(const float rootRows[16], const float partRows[16], float q[4], float at[3]);

// A shot's muzzle (row 3) and direction (row c, a round leaves along +Z) from fireBarrel's Mat4; false
// unless all six are finite and the muzzle within 100000 m.
bool shotFromMatrix(const float m[16], float p[3], float d[3]);
