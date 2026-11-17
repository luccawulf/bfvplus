// The fire hook (see fire_hook.h). Moved out of the probe (Task 5) when the recorder began to write
// shots (Task 9): Session D showed the client calls fireBarrel for every player's rounds (1,803 of 1,805
// matched the server's `f` lines), so one hook serves both.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "bfv_addresses.h"
#include "fire_hook.h"
#include "plus_log.h"
#include "replay.h"
#include "replay_probe.h"
#include "replay_settings.h"
#include "seh_read.h"

namespace {

typedef void(__thiscall* FireBarrelFn)(void* self, void* player, void* mat, int index);

// The Mat4 is an argument fireBarrel reads, so both observers see it before the call.
void __fastcall hook_fireBarrel(void* self, void* /*edx*/, void* player, void* mat, int index)
{
    if (g_replay.probe) probe_onFire((uintptr_t)self, (uintptr_t)player, (uintptr_t)mat, index);
    if (g_replay.record) replay_onFire((uintptr_t)self, (uintptr_t)player, (uintptr_t)mat);
    ((FireBarrelFn)bfv::FIRE_BARREL_FN)(self, player, mat, index);
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

bool installFireHook()
{
    static bool done = false;
    if (done) return true;
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) return false;
    const BfvSite* sites[] = { &SITE_fireBarrelCall1, &SITE_fireBarrelCall2, &SITE_fireBarrelCall3, &SITE_fireBarrelCall4,
        &SITE_fireBarrelCall5, &SITE_fireBarrelCall6 };
    if (!bytesAt(SITE_fireBarrelEntry.va, SITE_fireBarrelEntry.bytes)) {
        plusLog("fire hook: fireBarrel's entry differs; not installed (no shots recorded)");
        return false;
    }
    for (const BfvSite* s : sites) {
        uint8_t* site = (uint8_t*)s->va;
        if (!bytesAt(s->va, s->bytes) || site[0] != 0xE8
            || (uintptr_t)(site + 5) + (uintptr_t)(*(int32_t*)(site + 1)) != bfv::FIRE_BARREL_FN) {
            plusLog("fire hook: fireBarrel call site 0x%08X differs; not installed (no shots recorded)", (unsigned)s->va);
            return false;
        }
    }
    for (const BfvSite* s : sites) {
        uint8_t* site = (uint8_t*)s->va;
        DWORD old;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
        *(int32_t*)(site + 1) = (int32_t)((uintptr_t)&hook_fireBarrel - (uintptr_t)(site + 5));
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    done = true;
    plusLog("fire hook on fireBarrel's six call sites");
    return true;
}
