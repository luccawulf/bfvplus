// The chat box's lines for the replay (see chat_hook.h).
//
// BfMenu::addPlayerChatMessage(std::wstring line, BFPlayer* player, int team) (0x004CC2A0, thiscall, ret
// 0x24) puts a player's line in the chat box: the line as shown, built by its caller ("Name: text"), the
// sender (null for a line with none) and his side. It checks the sender against the chat box's ignore list
// (+0x1DB4) and hands the line to the box (0x004CBE80). BF1942.exe's (0x006A8E10) is the function BF42++
// hooks for its `chat` records (bf/ui.cpp addPlayerChatMessage_hook, replay.cpp replay_onChat). Seven call
// sites in v1.21, and no other reference to it (static RE 2026-11-23, every E8 in .text): the chat input's
// two (0x004EE8D2, 0x004EED83, the sender's own line, which the server never sends him back), the incoming
// lines' two (0x0051DF4F, 0x0051DFA7), the console's message switch (0x004CC6B4, type 1) and two more
// (0x00526098, 0x00529437). The owner's patched exe has the same bytes at all eight places.
//
// Each call site's rel32 goes to `hook_addPlayerChat`, which copies the line out (a copy, read SEH-safe:
// the original owns the string and frees it) and queues it for the recorder (`replay_onChat`), then calls
// the original with the same arguments. Nothing is written here: the recorder's locked paths write the
// queued lines, each at its own time (`drainShots`), as they do the fire hook's rounds.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "bfv_addresses.h"
#include "chat_hook.h"
#include "plus_log.h"
#include "replay.h"
#include "replay_settings.h"
#include "seh_read.h"

namespace {

// The game's std::wstring (MSVC 7.1) as a caller passes it by value: 28 bytes, the characters in place
// while the capacity is under 8 (+4), else a pointer to them there; the length at +0x14, the capacity at
// +0x18 (0x005BB8DB writes 7 and 0 into a fresh one).
struct GameWString {
    uint32_t proxy;
    union { uint16_t buf[8]; const uint16_t* ptr; } u;
    uint32_t size;
    uint32_t cap;
};
static_assert(sizeof(GameWString) == 28, "the game passes 28 bytes");

typedef void(__thiscall* AddPlayerChatFn)(void* menu, GameWString line, void* player, int team);

bool g_installed = false;

void __fastcall hook_addPlayerChat(void* menu, void* /*edx*/, GameWString line, void* player, int team)
{
    // Only with every path into the box hooked (`g_installed`): a file's whole lines then stand for all its
    // chat, and the viewer leaves its pieces.
    if (g_replay.record && g_installed) {
        uint16_t units[256];
        size_t n = line.size < 255 ? line.size : 255;
        bool ok = true;
        if (line.cap >= 8) ok = n == 0 || seh::read((uintptr_t)line.u.ptr, units, n * sizeof(uint16_t));
        else std::memcpy(units, line.u.buf, (n < 8 ? n : 8) * sizeof(uint16_t));
        int pid = -1;
        uint32_t id = 0;
        if (player && seh::read((uintptr_t)player + bfv::PLAYER_ID, &id, sizeof(id)) && id < 256) pid = (int)id;
        if (ok) replay_onChat(pid, team, units, n);
    }
    ((AddPlayerChatFn)bfv::ADD_PLAYER_CHAT_FN)(menu, line, player, team);
}

bool bytesAt(uintptr_t va, const char* hex)
{
    uint8_t want[16];
    size_t n = 0;
    for (const char* p = hex; *p && n < sizeof(want);) {
        while (*p == ' ') ++p;
        if (!p[0] || !p[1]) break;
        char b[3] = { p[0], p[1], 0 };
        want[n++] = (uint8_t)std::strtoul(b, nullptr, 16);
        p += 2;
    }
    uint8_t have[16];
    return n && seh::read(va, have, n) && std::memcmp(have, want, n) == 0;
}

}  // namespace

bool chatHookInstalled() { return g_installed; }

bool installChatHook()
{
    static bool done = false;
    if (done) return g_installed;
    done = true;
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) return false;
    const BfvSite* sites[] = { &SITE_addPlayerChatCall1, &SITE_addPlayerChatCall2, &SITE_addPlayerChatCall3,
        &SITE_addPlayerChatCall4, &SITE_addPlayerChatCall5, &SITE_addPlayerChatCall6, &SITE_addPlayerChatCall7 };
    if (!bytesAt(SITE_addPlayerChatEntry.va, SITE_addPlayerChatEntry.bytes)) {
        plusLog("chat hook: addPlayerChatMessage's entry differs; not installed (no whole chat lines recorded)");
        return false;
    }
    for (const BfvSite* s : sites) {
        uint8_t* site = (uint8_t*)s->va;
        if (!bytesAt(s->va, s->bytes) || site[0] != 0xE8
            || (uintptr_t)(site + 5) + (uintptr_t)(*(int32_t*)(site + 1)) != bfv::ADD_PLAYER_CHAT_FN) {
            plusLog("chat hook: addPlayerChatMessage call site 0x%08X differs; not installed (no whole chat lines recorded)",
                (unsigned)s->va);
            return false;
        }
    }
    int patched = 0;
    for (const BfvSite* s : sites) {
        uint8_t* site = (uint8_t*)s->va;
        DWORD old;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
        *(int32_t*)(site + 1) = (int32_t)((uintptr_t)&hook_addPlayerChat - (uintptr_t)(site + 5));
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        ++patched;
    }
    // A file says it has the chat box's lines only when every path into the box is hooked: the viewer then
    // reads them alone and leaves the pieces (a site left out would lose its lines from the replay).
    g_installed = patched == 7;
    if (g_installed) plusLog("chat hook on addPlayerChatMessage's seven call sites");
    else plusLog("chat hook on %d of addPlayerChatMessage's seven call sites (the others could not be made writable); "
                 "files keep the chat pieces alone", patched);
    return g_installed;
}
