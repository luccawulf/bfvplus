// bfvplus - DLL hijack for Battlefield Vietnam
//
// Drop this DLL into the BFV install directory as dinput8.dll.
// Windows loads it instead of the system dinput8.dll because of the search
// order. We forward DirectInput8Create to the real system DLL, then on
// DLL_PROCESS_ATTACH we install our hook into BfVietnam.exe.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include "buddycolors.h"
#include "config.h"
#include "capture.h"
#include "sensitivity.h"
#include "replay.h"
#include "replay_settings.h"

// Real dinput8.dll function we forward to.
// LPUNKNOWN is declared as void* here to avoid pulling in <Unknwn.h> - we
// just pass the pointer through to the real function untouched.
typedef HRESULT(WINAPI* DirectInput8Create_t)(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
    LPVOID* ppvOut, void* punkOuter);

static HMODULE g_realDInput8 = nullptr;
static DirectInput8Create_t g_realDirectInput8Create = nullptr;

// Load the real system dinput8.dll. We can't just LoadLibraryA("dinput8.dll")
// because that finds us again (we ARE dinput8.dll from the game's perspective).
// Build an absolute path to System32. On 64-bit Windows, 32-bit processes hit
// SysWOW64 automatically via filesystem redirection.
static bool loadRealDInput8()
{
    char path[MAX_PATH];
    UINT len = GetSystemDirectoryA(path, MAX_PATH);
    if (len == 0 || len > MAX_PATH - 16) return false;

    lstrcatA(path, "\\dinput8.dll");

    g_realDInput8 = LoadLibraryA(path);
    if (!g_realDInput8) return false;

    g_realDirectInput8Create = (DirectInput8Create_t)
        GetProcAddress(g_realDInput8, "DirectInput8Create");

    return g_realDirectInput8Create != nullptr;
}

// Exported function. The game calls this expecting the real dinput8 behavior.
extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
    LPVOID* ppvOut, void* punkOuter)
{
    if (!g_realDirectInput8Create) return E_FAIL;
    HRESULT hr = g_realDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);

    // Hook CreateDevice on the interface we just handed back, so we can see the
    // mouse device get created and scale its X axis. This is the whole reason the
    // sensitivity fix does not need BFV's equivalent of addPlayerInput - see
    // sensitivity.cpp. Self-gates if the feature is off or the binary looks wrong.
    if (SUCCEEDED(hr) && ppvOut && *ppvOut) installSensitivityHooks(*ppvOut);

    return hr;
}

// Initialization thread. Runs once on attach.
// We do work on a separate thread because DllMain holds the loader lock,
// and calling LoadLibrary or doing anything substantive inside it can
// deadlock the process.
static DWORD WINAPI initThread(LPVOID)
{
    // Sanity check: are we actually loaded into BfVietnam.exe v1.21?
    // Module base must be 0x400000 and the bytes at the call site we're
    // about to patch (0x57A1E4) must match what we found in our analysis:
    // E8 47 10 FD FF  (call rel32 to 0x54B230).
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) {
        return 0;
    }

    // Seed the RNG so shift-click random colors aren't predictable across
    // game sessions.
    srand((unsigned)GetTickCount());

    // Load INI (preset colors + [capture] enabled).
    loadConfig();

    // SSM capture: hook the received-GameEvent reader. installCaptureHook does its
    // own binary sanity check and is a no-op if disabled or the bytes don't match.
    // The replay recorder and its probe hang off the same hook, so it goes in when any of the
    // three wants it.
    if (g_captureEnabled || g_replay.record || g_replay.probe) installCaptureHook();


    // Stationary-MG sensitivity needs the InfantryControls / LandSeaControls objects,
    // which we capture at their (single) construction sites. This has to be in place
    // before the game builds its settings root, so install it here rather than lazily.
    // Each patch verifies the call target first and is a no-op on a mismatch.
    if (g_mgInfSensEnabled) installControlSettingsHooks();

    // AddBuddy color cycling: only if the call site matches what we patched against
    // (a community patch may have moved it). Guard here so a mismatch doesn't also
    // disable capture above.
    const uint8_t expected[] = { 0xE8, 0x47, 0x10, 0xFD, 0xFF };
    const uint8_t* target = (const uint8_t*)0x0057A1E4;
    bool buddyOk = true;
    for (int i = 0; i < 5; ++i) {
        if (target[i] != expected[i]) { buddyOk = false; break; }
    }
    if (buddyOk) installAddBuddyHook();

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);

        // Load the real dinput8.dll. If this fails, the game crashes when
        // it calls DirectInput8Create through us - but there's nothing we
        // can do, we MUST proxy this export.
        if (!loadRealDInput8()) return FALSE;

        // Kick off initialization on a separate thread to avoid the loader
        // lock. The game won't have started its main loop yet, so it won't
        // hit our patched call site until after the hook is in.
        HANDLE h = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    } else if (reason == DLL_PROCESS_DETACH) {
        // Game exit: close an open replay file with its end line (no-op when not recording).
        replay_shutdown();
    }
    return TRUE;
}
