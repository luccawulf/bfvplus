// AddBuddy button color cycling.
//
// In BFV, clicking "Add Buddy" on the scoreboard calls a handler at 0x57a1c0
// which loads the player ID from the button object at [ecx+0x4c], finds the
// buddy list manager at [[0xd59de8]+0x1c30], and calls addPlayerToBuddyList
// at 0x54b230. The default impl adds them in green (0, 255, 0).
//
// We redirect the call instruction at 0x57a1e4 (the only `call 0x54b230` in
// the scoreboard UI) to our own handler. Our handler:
//   - Reads the player ID and the player's name from the game's player list.
//   - Checks if the player is already a buddy via isPlayerIDInBuddyList.
//   - If not, calls 0x54b230 unchanged (game adds them in default green).
//   - If yes, picks the next color (cycle or random with shift) and calls
//     addPlayerToBuddyListByName at 0x546ff0 with the chosen color.
//
// Calling 0x546ff0 with an existing buddy name updates their color in-place
// rather than adding a duplicate - this is the same mechanism the console
// command `game.addPlayerToBuddyListByName` uses.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "buddycolors.h"

#include <cstdlib>

// =============================================================================
// Game addresses (BfVietnam.exe v1.21, image base 0x00400000)
// =============================================================================
//
// All discovered through static analysis of BfVietnam.exe. See README for
// the methodology.

// The `call 0x54b230` instruction inside the AddBuddy button handler.
// We overwrite these 5 bytes to redirect to our hook.
static constexpr uintptr_t ADDR_call_addBuddy = 0x0057A1E4;

// addPlayerToBuddyList(int playerId) -> default-color add. Takes
// ECX=buddyMgr, stack arg playerId. Used for the first-click "add new
// buddy" path, which we keep unchanged.
typedef void (__fastcall* addPlayerToBuddyList_t)(void* this_, void* edx, int playerId);
static const addPlayerToBuddyList_t addPlayerToBuddyList =
    (addPlayerToBuddyList_t)0x0054B230;

// isPlayerIDInBuddyList(int playerId) -> color* or null.
// Takes ECX=buddyMgr, stack arg playerId. The returned pointer is to
// 3 contiguous floats (R, G, B in 0..255 range) stored inside the
// buddy list entry. Writing to these floats changes the color the
// game uses for nametags, scoreboard, chat, kill messages, etc.
typedef void* (__fastcall* isPlayerIDInBuddyList_t)(void* this_, void* edx, int playerId);
static const isPlayerIDInBuddyList_t isPlayerIDInBuddyList =
    (isPlayerIDInBuddyList_t)0x00545CA0;

// saveBuddyListToFile() at 0x545FE0. The real save function - called by
// the game itself at the end of addPlayerToBuddyListByName (0x54B280) so
// every buddy mutation persists.
//
// Identified by its body at 0x5460BB-0x546199: it walks the buddy map,
// reads each entry's color floats from [node+0x28] +0/+4/+8, formats them
// with the "%s\" %5.1f %5.1f %5.1f\n" style format string at 0xB69270,
// and writes the file at "<profile>/Settings/Profiles/BuddyList.con".
//
// Takes ECX = buddy manager (same `this` as the rest of the buddy API).
typedef void (__fastcall* saveBuddyListToFile_t)(void* this_, void* edx);
static const saveBuddyListToFile_t saveBuddyListToFile =
    (saveBuddyListToFile_t)0x00545FE0;

// Player list manager global pointer.
static constexpr uintptr_t ADDR_g_playerList = 0x00D7D01C;

// =============================================================================
// Preset color list (from bfvplus.ini)
// =============================================================================

// Default cycle if no INI configuration: green, yellow, red, light blue.
// Matches the spirit of bf42plus's default presets.
static std::vector<uint32_t> s_presetColors = {
    0x00FF00,  // green (BFV default)
    0xFFFF00,  // yellow
    0xFF0000,  // red
    0x00AAFF,  // light blue
};

const std::vector<uint32_t>& getPresetColors() { return s_presetColors; }

// Loaded by config.cpp - defined here as the storage owner.
void setPresetColors(const std::vector<uint32_t>& colors)
{
    if (colors.empty()) return;  // keep defaults if INI list is empty
    s_presetColors = colors;
}

// =============================================================================
// Color cycling logic
// =============================================================================

// Find which preset index matches a given color. Returns -1 if no match.
// The current buddy color comes from isPlayerIDInBuddyList as 3 floats in
// 0..255 range, which we round to ints for comparison.
static int findPresetIndex(const BuddyColor& current)
{
    int r = (int)(current.r + 0.5f);
    int g = (int)(current.g + 0.5f);
    int b = (int)(current.b + 0.5f);
    uint32_t packed = packRGB(r, g, b);

    for (size_t i = 0; i < s_presetColors.size(); ++i) {
        if (s_presetColors[i] == packed) return (int)i;
    }
    return -1;
}

// Pick the next color for this buddy. If shift is held or there's no preset
// list, return a random color. Otherwise advance to the next preset (wrap
// around). If the current color isn't in the list, start at preset 0.
static uint32_t pickNextColor(const BuddyColor& current)
{
    bool shift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
                 (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;

    if (shift || s_presetColors.empty()) {
        // Random color. We avoid VERY dark colors since they'd be invisible
        // against the dark UI background.
        uint32_t v = (uint32_t)rand();
        v ^= ((uint32_t)rand() << 8);
        v &= 0xFFFFFF;
        // Boost any channel that's too dark to be readable.
        int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
        int maxc = r; if (g > maxc) maxc = g; if (b > maxc) maxc = b;
        if (maxc < 0x80) {
            // brighten all channels proportionally so the highest hits 0xFF
            int boost = 0xFF - maxc;
            r += boost; g += boost; b += boost;
            if (r > 0xFF) r = 0xFF;
            if (g > 0xFF) g = 0xFF;
            if (b > 0xFF) b = 0xFF;
            v = packRGB(r, g, b);
        }
        return v;
    }

    int idx = findPresetIndex(current);
    if (idx < 0) return s_presetColors[0];
    return s_presetColors[(idx + 1) % s_presetColors.size()];
}

// =============================================================================
// The hook itself
// =============================================================================
//
// This replaces a CALL instruction, not a function entry. The original
// instruction was a 5-byte CALL with ECX = buddyMgr and one stack arg
// (playerId) already pushed. Our function has the same signature as
// addPlayerToBuddyList (which it's replacing the call to).

static void __fastcall hook_AddBuddyButtonClicked(
    void* buddyMgr, void* edx, int playerId)
{
    // Is this player already a buddy?
    void* curColorPtr = isPlayerIDInBuddyList(buddyMgr, edx, playerId);
    if (!curColorPtr) {
        // Not a buddy yet. Do exactly what the game would have done:
        // add them with default green color.
        addPlayerToBuddyList(buddyMgr, edx, playerId);
        return;
    }

    // Already a buddy. Pick the next color and write directly to the
    // buddy entry's color struct. The pointer returned by
    // isPlayerIDInBuddyList points to 3 floats that every UI consumer
    // (nametag, scoreboard, chat, kill messages) reads each frame to
    // pick colors, so writing here updates everything at once.
    BuddyColor* cur = (BuddyColor*)curColorPtr;
    uint32_t newColor = pickNextColor(*cur);

    cur->r = (float)((newColor >> 16) & 0xFF);
    cur->g = (float)((newColor >> 8) & 0xFF);
    cur->b = (float)(newColor & 0xFF);

    // Persist the change to disk by calling the game's own save function.
    // 0x545FE0 walks the buddy map and writes each entry's name + RGB to
    // Settings/Profiles/<profile>/BuddyList.con. The game itself calls
    // this at the end of addPlayerToBuddyListByName after every mutation,
    // which is why native console commands persist - we just mirror that.
    saveBuddyListToFile(buddyMgr, edx);
}

// =============================================================================
// Hook installation
// =============================================================================
//
// We're patching a 5-byte `call rel32` instruction, not installing a
// trampoline. The call's destination is encoded as a relative offset:
//   bytes:    E8 xx xx xx xx
//   rel32 = target - (call_addr + 5)
// We just rewrite rel32 to point at our hook.

void installAddBuddyHook()
{
    uint8_t* site = (uint8_t*)ADDR_call_addBuddy;

    // Sanity check: this should be a `call rel32` (opcode E8).
    // Original target: 0x54B230. Original rel32 = 0x54B230 - (0x57A1E4 + 5) = 0xFFFD1047.
    if (site[0] != 0xE8) return;

    int32_t orig_rel = *(int32_t*)(site + 1);
    uintptr_t orig_target = (uintptr_t)(site + 5) + (uintptr_t)orig_rel;
    if (orig_target != 0x0054B230) return;  // not what we expected

    DWORD oldProtect;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return;

    int32_t new_rel = (int32_t)((uintptr_t)&hook_AddBuddyButtonClicked
                                 - (uintptr_t)(site + 5));
    *(int32_t*)(site + 1) = new_rel;

    VirtualProtect(site, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}
