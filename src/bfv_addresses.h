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
VTABLE(VT_PlayerControlObject, 0x00B93C44, "capture: live x32dbg 2026-06-03 (PCO template at +0x08)")
VTABLE(VT_ObjectManager, 0x00B90A8C, "port notes: findallmem live 2026-06-03; global *(0x00D7D014)")
VTABLE(VT_ObjectTemplateManager, 0x00B95334, "capture: global *(0x00D7D018), live 2026-06-03")
VTABLE(VT_BFSoldier, 0x00B9033C, "sensitivity: RTTI locator 0x00C3EF08, slot 39 = handlePlayerInput 0x0065F350")
