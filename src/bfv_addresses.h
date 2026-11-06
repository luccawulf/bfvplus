// Every BfVietnam.exe address bfvplus patches or reads, in one table: what it is, where it was
// found, and (for code patches) the bytes that must be there first. tools/check_exes.py parses the
// SITE lines to check any exe offline. Retail v1.21, image base 0x00400000.
//
// The modules that already patched a site before this table existed (buddycolors, capture,
// sensitivity) keep their own checks; their sites are listed here so the exe check covers them.
#pragma once
#include <cstdint>

struct BfvSite { uintptr_t va; const char* bytes; const char* how; };
struct BfvVtable { uintptr_t va; const char* how; };

#define SITE(name, va, bytes, how) inline constexpr BfvSite name{ va, bytes, how };
#define VTABLE(name, va, how) inline constexpr BfvVtable name{ va, how };

// ---- code patches (call rel32 sites; the bytes are the original instruction) ----------------
SITE(SITE_addBuddyCall, 0x0057A1E4, "E8 47 10 FD FF", "buddycolors: scoreboard Add Buddy -> addPlayerToBuddyList 0x0054B230")
SITE(SITE_readEventCall, 0x005B3C14, "E8 57 FE FF FF", "capture: packet deserializer 0x005B3B50 -> readEvent 0x005B3A70 (static RE, live 2026-06-03)")
SITE(SITE_infCtorCall, 0x004E0939, "E8 A2 37 0C 00", "sensitivity: settings root -> InfantryControls::ctor 0x005A40E0")
SITE(SITE_landSeaCtorCall, 0x004E0956, "E8 05 28 0C 00", "sensitivity: settings root -> LandSeaControls::ctor 0x005A3160")

// ---- vtables (identify live objects; not patched) --------------------------------------------
VTABLE(VT_PlayerControlObject, 0x00B93D24, "RTTI (tools/rtti_vtables.py): PlayerControlObject, subobject +0x0")
VTABLE(VT_PlayerControlObject_160, 0x00B93C44, "RTTI: the PCO subobject at +0x160 (capture.cpp scans for this one: its +0x08 template, +0x120 body)")
VTABLE(VT_BFSoldier_160, 0x00B902F0, "RTTI: BFSoldier's subobject at +0x160 (soldiers don't carry 0x00B93C44)")
VTABLE(VT_BFPlayer, 0x00B5F70C, "RTTI: BFPlayer@bf@dice, subobject +0x0")
VTABLE(VT_PlayerManager, 0x00BA413C, "RTTI: PlayerManager@world@ref2@dice; *(0x00D7D01C) points at it (Session A)")
VTABLE(VT_ObjectManager, 0x00B90A8C, "port notes: findallmem live 2026-06-03; global *(0x00D7D014)")
VTABLE(VT_ObjectTemplateManager, 0x00B95334, "capture: global *(0x00D7D018), live 2026-06-03")
VTABLE(VT_BFSoldier, 0x00B9033C, "sensitivity: RTTI locator 0x00C3EF08, slot 39 = handlePlayerInput 0x0065F350")

// ---- the replay recorder's reads (Session A on the lab, 2026-11-06; evidence in the viewer repo's
//      features/bfv-client-recorder/session-a.md) ------------------------------------------------
namespace bfv {
// Objects. ObjectManager::registerObject (vtable 0x00B90A8C slot 56) keys a stdext::hash_map at
// mgr+0x110 by obj+0x48; that hash_map's list head pointer is at +0x08 and its size at +0x0C. A list
// node is {next +0, prev +4, key +8, object +0xC} (the lookup at 0x005B6600 compares node+8).
inline constexpr uintptr_t OBJMGR_GLOBAL = 0x00D7D014;    // *() -> ObjectManager
inline constexpr uintptr_t OBJMGR_LIST_HEAD = 0x118;      // mgr+0x110 hash_map, +0x08
inline constexpr uintptr_t OBJMGR_LIST_SIZE = 0x11C;      // mgr+0x110 hash_map, +0x0C
inline constexpr uintptr_t LIST_NEXT = 0x00, LIST_OBJECT = 0x0C;
// An object (Object; PlayerControlObject and BFSoldier alike). Matches the server's
// target_bfv_linded.c up to +0x6C; the absolute transform is 4 bytes later than the server's.
inline constexpr uintptr_t OBJ_FLAGS = 0x04;     // 0x8000 root; 0x40000 cached transform is stale
inline constexpr uintptr_t OBJ_ID = 0x48;        // the object manager's id (BF42++'s "gid")
inline constexpr uintptr_t OBJ_TEMPLATE = 0x4C;  // -> template; its name is an MSVC string at +0x0C
inline constexpr uintptr_t OBJ_PARENT = 0x50;    // null for a root
inline constexpr uintptr_t OBJ_NETWORKABLE = 0x6C;  // -> *Networkable; the net id is the u16 at +0x04
inline constexpr uintptr_t OBJ_ROWS = 0x94;      // rows a, b, c (transpose of the server quaternion)
inline constexpr uintptr_t OBJ_POS = 0xC4;       // position, then w = 1
inline constexpr unsigned VSLOT_ABS_TRANSFORM = 15;  // 0x00684990: refreshes a stale child, returns obj+0x94
inline constexpr uintptr_t NET_ID = 0x04;
inline constexpr uintptr_t TMPL_NAME = 0x0C;     // MSVC 7.1 string: 16-byte buffer, size +0x10, capacity +0x14
// Players. PlayerManager vtable 0x00BA413C: slot 9 returns [pm+0x54] (the local player), slot 14
// returns pm+0x0C (the player list: an MSVC list, head pointer at pm+0x10, node {next, prev, player}).
inline constexpr uintptr_t PLAYERMGR_GLOBAL = 0x00D7D01C;
inline constexpr uintptr_t PM_LIST_HEAD = 0x10;
inline constexpr uintptr_t PM_NODE_PLAYER = 0x08;
inline constexpr unsigned VSLOT_LOCAL_PLAYER = 9;   // sensitivity.cpp ships this call
inline constexpr unsigned VSLOT_PLAYER_VEHICLE = 14;
inline constexpr uintptr_t PLAYER_NAME = 0x50;   // MSVC string (41 of 41 joined by name)
inline constexpr uintptr_t PLAYER_ID = 0x7C;     // u32 (41 of 41)
inline constexpr uintptr_t PLAYER_TEAM = 0xC0;   // u32 (41 of 41)
inline constexpr uintptr_t PLAYER_AI = 0xBC;     // u8, just before team as on the server: 0 for the owner, 1 for bots (Session A's 65 owner and 2,861 bot dumps; Session B showed +0xC4 is 1 for everyone)
// Events: the payload starts at ev+12 and carries the server recorder's BFV layout (core.c's offsets).
inline constexpr uintptr_t EVENT_PAYLOAD = 0x0C;
}  // namespace bfv
