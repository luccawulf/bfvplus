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
// The probe's fire hook (plan-1b Task 5; the recorder's in Task 9): FireArms::Fire 0x0067F170 calls
// FireArms::fireBarrel(IPlayer*, Mat4&, int) 0x0067D7E0 from these six call sites and nowhere else
// (static RE 2026-11-06: the server's fireBarrel 0x087E0D30 has five call sites, all in its Fire).
SITE(SITE_fireBarrelCall1, 0x0067F7EC, "E8 EF DF FF FF", "Fire 0x0067F170 -> fireBarrel 0x0067D7E0, push -1 (static RE)")
SITE(SITE_fireBarrelCall2, 0x0067F80C, "E8 CF DF FF FF", "Fire -> fireBarrel, push 0 (static RE)")
SITE(SITE_fireBarrelCall3, 0x0067F869, "E8 72 DF FF FF", "Fire -> fireBarrel, the barrel loop, push edi (static RE)")
SITE(SITE_fireBarrelCall4, 0x0067F8B3, "E8 28 DF FF FF", "Fire -> fireBarrel, push the byte at FireArms+0x332 (static RE)")
SITE(SITE_fireBarrelCall5, 0x0067F90F, "E8 CC DE FF FF", "Fire -> fireBarrel, push esi (static RE)")
SITE(SITE_fireBarrelCall6, 0x0067F93B, "E8 A0 DE FF FF", "Fire -> fireBarrel, push edi (static RE)")
SITE(SITE_fireBarrelEntry, 0x0067D7E0, "81 EC C0 00 00 00 53 55", "fireBarrel's entry: sub esp,0xC0; push ebx; push ebp (thiscall, ret 0xC)")

// ---- vtables (identify live objects; not patched) --------------------------------------------
VTABLE(VT_PlayerControlObject, 0x00B93D24, "RTTI (tools/rtti_vtables.py): PlayerControlObject, subobject +0x0")
VTABLE(VT_PlayerControlObject_160, 0x00B93C44, "RTTI: the PCO subobject at +0x160 (capture.cpp scans for this one: its +0x08 template, +0x120 body)")
VTABLE(VT_BFSoldier_160, 0x00B902F0, "RTTI: BFSoldier's subobject at +0x160 (soldiers don't carry 0x00B93C44)")
VTABLE(VT_BFPlayer, 0x00B5F70C, "RTTI: BFPlayer@bf@dice, subobject +0x0")
VTABLE(VT_PlayerManager, 0x00BA413C, "RTTI: PlayerManager@world@ref2@dice; *(0x00D7D01C) points at it (Session A)")
VTABLE(VT_ObjectManager, 0x00B90A8C, "port notes: findallmem live 2026-06-03; global *(0x00D7D014)")
VTABLE(VT_ObjectTemplateManager, 0x00B95334, "capture: global *(0x00D7D018), live 2026-06-03")
VTABLE(VT_BFSoldier, 0x00B9033C, "sensitivity: RTTI locator 0x00C3EF08, slot 39 = handlePlayerInput 0x0065F350")
// plan-1b static RE (2026-11-06): tools/rtti_vtables.py, the primary (+0x0) vtable of each class.
VTABLE(VT_Armor, 0x00B73844, "RTTI Armor@world@ref2@dice; a component in an object's map, id 0xC4A4")
VTABLE(VT_Engine, 0x00BA486C, "RTTI Engine@world@ref2@dice (+0x84: 0x00BA4854)")
VTABLE(VT_PhysicsEngine, 0x00BA498C, "RTTI PhysicsEngine@world@ref2@dice; an Engine's +0x64")
VTABLE(VT_ScoreManager, 0x00B739EC, "RTTI ScoreManager@world@ref2@dice (+0x4: 0x00B739B8); *(0x00D65748)")
VTABLE(VT_ControlPoint, 0x00B90C94, "RTTI ControlPoint@world@ref2@dice (+0x84 0x00B90C7C, +0x160 0x00B90C50)")
VTABLE(VT_FireArms, 0x00B9164C, "RTTI FireArms@world@ref2@dice (+0x84 0x00B91630, +0x160 0x00B91604)")
VTABLE(VT_HandFireArms, 0x00B9D97C, "RTTI HandFireArms@world@ref2@dice (+0x84, +0x160, +0x368 subobjects)")
VTABLE(VT_RotationalBundle, 0x00BA45FC, "RTTI RotationalBundle@world@ref2@dice (+0x84: 0x00BA45E0)")
VTABLE(VT_MusicPlayer, 0x00B90654, "RTTI MusicPlayer@world@ref2@dice (+0x84: 0x00B90638)")

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

// ---- stages 2-3 (plan-1b), from static RE on 2026-11-06 unless marked "estimate". "L" is the Linux
//      server bfv_linded (symbols); a client vtable slot n is L's slot n+1 (one MSVC destructor slot
//      where GCC has two), checked on each getter's body. Session D confirms or corrects each. --------
inline constexpr uintptr_t OBJ_REL_PTR = 0x8C;   // -> the relative transform (Session A); a child's own turn
// An object's components: Object::queryComponent (client slot 9, 0x00684A30) takes five ids from fields
// (+0x5C..+0x6C, the networkable at +0x6C), else finds the id in the std::map at obj+0xD8 and compares
// the node with the head at obj+0xDC; the component is node+0x10. MSVC 7.1 _Tree node: left +0, parent
// +4, right +8, key +0x0C, value +0x10, then the colour and isnil bytes.
inline constexpr uintptr_t COMP_MAP_HEAD = 0xDC;
inline constexpr uintptr_t COMP_NODE_KEY = 0x0C, COMP_NODE_VALUE = 0x10;
inline constexpr uint32_t ARMOR_IID = 0xC4A4;    // 0x006945D0 calls queryComponent(0xC4A4, 0xC4A4); the server's armor_iid
// Armor (vtable 0x00B73844): client slots 8, 6, 12, 31 = L getHitPoints, getMaxHitPoints,
// getCriticalDamage, getLastHitPlayer; 51, 52 = isDestroyed, isCriticalDamaged; 36 = getObject.
inline constexpr uintptr_t ARMOR_HP = 0x38, ARMOR_MAXHP = 0x3C, ARMOR_CRIT = 0xF0, ARMOR_LASTHIT = 0x14;
inline constexpr uintptr_t ARMOR_DESTROYED = 0x110, ARMOR_CRITICAL = 0x111, ARMOR_OBJECT = 0x34;
// Engine (vtable 0x00BA486C): handleMessage 0x006F95F0 (message 4 sets the running byte unless the
// disabled byte is set, 5 clears it; the physics node at +0x64); handlePlayerInput 0x006F9810 writes
// the input vector at +0x178, its third axis +0x180 being the server's throttle (L +0x160);
// handleUpdate 0x006F9950 zeroes the PhysicsEngine's revs +0xB0 when not running and steps its gear +0xD0.
inline constexpr uintptr_t ENG_PHYS = 0x64, ENG_RUNNING = 0x1A0, ENG_DISABLED = 0x1A1, ENG_THROTTLE = 0x180;
inline constexpr uintptr_t PE_REVS = 0xB0, PE_GEAR = 0xD0;
// ScoreManager: 0x00573F28 loads *(0x00D65748), calls slot 5 (getTeamScore 0x0059A080: this + 0x28 +
// team * 0x44) and reads +0x38 (the tickets, TeamScore::setTickets on L); 0x004C112F stores the new manager.
inline constexpr uintptr_t SCOREMGR_GLOBAL = 0x00D65748;
inline constexpr uintptr_t SCORE_TEAM_BASE = 0x28, SCORE_TEAM_STRIDE = 0x44, SCORE_TICKETS = 0x38;
// ControlPoint (vtable 0x00B90C94): handleUpdate 0x00674B40 against L 0x087D70A0 puts every field 0x24
// later (template +0x1C4, timer +0x1EC, count +0x1E8, attack +0x1F0/+0x1F4), so L's team +0x1B0
// (ControlPoint::setTeam 0x087D7B70) is +0x1D4; the update compares its winner with it.
inline constexpr uintptr_t CP_TEAM = 0x1D4, CP_TEMPLATE = 0x1C4;
// Shots: FireArms::Fire 0x0067F170 (thiscall, 1 argument), found by its call order inside
// FireArms::handleUpdate 0x006809D0; fireBarrel(IPlayer*, Mat4&, int) 0x0067D7E0, ret 0xC; its Mat4 is
// 16 floats like an object's rows (a, b, c, position; a round leaves along row c).
inline constexpr uintptr_t FIRE_FN = 0x0067F170, FIRE_BARREL_FN = 0x0067D7E0;
// Animation: getCurrentStateFlags 0x0072C3F0 reads an instance's state +0x18, then *(0x00DE450C)'s
// vector of AnimationState* (begin +0x18, end +0x1C) and the state's flags +0x2C.
inline constexpr uintptr_t ANIM_ASM_GLOBAL = 0x00DE450C;
inline constexpr uintptr_t ASM_STATES = 0x18, STATE_FLAGS = 0x2C, INSTANCE_STATE = 0x18;
// Soldier (BFSoldier): instances at +0x2F0, +0x330, +0x370, +0x3B0 (stride 0x40; 51 and 29 `lea` of the
// first two before instance calls). The server's lower/upper are +0x2C4/+0x304. Session D (2026-11-17,
// tests/fixtures/soldiers.txt): +0x2F0 is the lower body and +0x330 the upper (within 200 m of the local
// player 133 of 135 and 127 of 135 equal the server's; farther away the client stops updating them), aim
// +0x2DC/+0x2E0 as estimated (92 %), and item and bits sit 4 past the estimate (+0x408 66 of 66 changes,
// +0x436 157 of 180), so the item is not the server's +0x3D8 plus 0x2C.
inline constexpr uintptr_t SOL_LOWER = 0x2F0 + 0x18, SOL_UPPER = 0x330 + 0x18;
inline constexpr uintptr_t SOL_ITEM = 0x408, SOL_BITS = 0x436;
inline constexpr uintptr_t SOL_PITCH = 0x2DC, SOL_TWIST = 0x2E0;
// MusicPlayer (vtable 0x00B90654), Session D: the radio played song 0, was set to song 1, stopped, played
// and stopped again, and the bytes followed the server's song events: playing the byte +0x161, the song
// +0x168 and the time left +0x16C, the server's +0x144 and +0x148 (target_bfv_linded.c) 0x24 later as with
// the control point. The group (always 0 there) is the server's +0x140 plus 0x24.
inline constexpr uintptr_t MUS_PLAYING = 0x161, MUS_GROUP = 0x164, MUS_SONG = 0x168, MUS_LEFT = 0x16C;
// The recording player's hit mark, Session D: a float on the local BFPlayer that jumps to 1.0 when one of
// his rounds hits (every rise within 0.2 s of a hit the server credited to him; BF42++'s BF1942 one is +0x1CC).
inline constexpr uintptr_t PLAYER_HITMARK = 0x1EC;
// Names, Session D: a control point's name is an MSVC string at +0x2AC of its template (355 of 355), an
// AnimationState's name an MSVC string at +0x12C of the state (3,328 of 3,328).
inline constexpr uintptr_t CPT_NAME = 0x2AC, STATE_NAME = 0x12C;
}  // namespace bfv
