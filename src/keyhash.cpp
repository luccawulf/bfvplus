// playerKeyHash override for Battlefield Vietnam. See keyhash.h for the what/why.
//
// RE (static, retail BfVietnam.exe v1.21, image base 0x00400000, MD5
// 0c54d62082bc59a793e52fc7004cd955):
//
// The CD-key auth-result handler FUN_0051fdc0 builds a property bag to send to the
// server. On a valid key it sets three properties in sequence, each as
// "select name -> build value std::string -> commit":
//
//   push "playerKeyHash"; call 0x402100         ; select property
//   push 0; lea ecx,[&out]; push ecx; mov ecx,edi; call 0x48e420   ; out = MD5(cdkey), 32 hex
//   lea ecx,[..]; call 0x5a7c60                 ; commit
//
// FUN_0048e420 is __thiscall(this /*ecx*/, std::string* out, int flag) and callee-
// cleans (ret 8) - confirmed by the absence of an `add esp,8` after the call site at
// 0x0051FF7B (bytes E8 A0 E4 F6 FF -> 0x0048E420). "playerKeyHash" (0x00B6173C) has a
// single xref, so this is the only build site.
//
// Our override is also exactly 32 hex chars - the SAME length as the real hash - so the
// safe, ABI-free approach is: let the real FUN_0048e420 build its std::string (size,
// capacity, heap/inline all handled by the game), then overwrite the 32 characters in
// place. No reallocation, no std::string construction on our side. We redirect just the
// one call site (the same `call rel32` patch bfvplus already uses for the capture and
// AddBuddy hooks), so nothing but the playerKeyHash build is touched.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include "keyhash.h"

char g_keyhashOverride[33] = { 0 };
bool g_haveKeyhashOverride  = false;
char g_localKeyhash[33]     = { 0 };
bool g_haveLocalKeyhash     = false;

// ---- game addresses (BfVietnam.exe v1.21) ----------------------------------
// The keyhash the server records is MD5(cdkey), and the client puts it on the wire as
// field 1 of the GameSpy CD-key challenge response. gcd_compute_response (FUN_005C70A0,
// __fastcall(char* cdkey /*ecx*/, char* challenge /*edx*/, char* output /*stack*/), ret 4)
// fills the 0x48-byte output buffer:
//   output[0x00..0x1F] = MD5(cdkey)                    (32 hex - THE KEYHASH, unsalted)
//   output[0x20..0x27] = 8-hex random
//   output[0x28..0x47] = MD5(cdkey + random + challenge)   (the salted proof)
// A stock CD-key master cross-checks field 1 against the proof, but an unpure server that
// does not validate just records field 1. We detour gcd_compute_response, let it build the
// real response, then overwrite the 32-char field 1 with the user's chosen value (same
// length, content-checked). The proof stays real; an unpure server reads field 1 -> our value.
// (The earlier hook on FUN_0051fdc0/0x0051FF7B was a telemetry event logger, not the wire.)
static const uintptr_t ADDR_gcdResponse = 0x005C70A0;  // gcd_compute_response

typedef void(__fastcall* gcdResponse_t)(char* cdkey, char* challenge, char* output);
static gcdResponse_t gcdResponse_orig = nullptr;       // -> trampoline (runs the real fn)

// ---- small standalone logger (append; own file, decoupled from capture's log) ----
static void buildLogPath(char* out, size_t cap)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { _snprintf(out, cap, "logs\\bfvplus_keyhash.log"); out[cap - 1] = 0; return; }
    for (DWORD i = n; i > 0; --i) { if (exe[i - 1] == '\\' || exe[i - 1] == '/') { exe[i] = 0; break; } }
    char dir[MAX_PATH];
    _snprintf(dir, sizeof(dir), "%slogs", exe); dir[sizeof(dir) - 1] = 0;
    CreateDirectoryA(dir, nullptr);
    _snprintf(out, cap, "%s\\bfvplus_keyhash.log", dir); out[cap - 1] = 0;
}

static void khLog(const char* fmt, ...)
{
    char path[MAX_PATH]; buildLogPath(path, sizeof(path));
    FILE* f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---- override value handling ----------------------------------------------
static bool isHex32(const char* s)
{
    int i = 0;
    for (; i < 32; ++i) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return s[32] == '\0';   // exactly 32, no trailing junk
}

// First n chars are all hex (no terminator requirement) - used to confirm a buffer really
// holds a key hash before we overwrite it.
static bool allHex(const char* s, int n)
{
    for (int i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

void setKeyhashOverride(const char* raw)
{
#ifdef BFVPLUS_PUBLIC
    // Public release: the keyhash override is a debugging aid for testing your own
    // unpure server with a known identity, and is deliberately not shipped. This is
    // a hard no-op so g_haveKeyhashOverride can never become true; the read-only
    // capture of your OWN keyhash below is unaffected.
    (void)raw;
    return;
#else
    if (!raw) return;
    // Tolerate surrounding whitespace; config.cpp already trims, but be defensive.
    char buf[64]; size_t j = 0;
    for (size_t i = 0; raw[i] && j < sizeof(buf) - 1; ++i) {
        unsigned char c = (unsigned char)raw[i];
        if (c <= ' ') continue;
        buf[j++] = (char)c;
    }
    buf[j] = '\0';
    if (!isHex32(buf)) {
        khLog("ignored [keyhash] override \"%s\" (need exactly 32 hex chars)", raw);
        return;
    }
    memcpy(g_keyhashOverride, buf, 33);
    g_haveKeyhashOverride = true;
#endif
}

// Fallback: read the first 32 hex chars from keyhash.txt next to BfVietnam.exe.
static void loadKeyhashTxtFallback()
{
#ifdef BFVPLUS_PUBLIC
    return;   // see setKeyhashOverride - not shipped in the public build
#else
    if (g_haveKeyhashOverride) return;
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    for (DWORD i = n; i > 0; --i) { if (path[i - 1] == '\\' || path[i - 1] == '/') { path[i] = 0; break; } }
    char file[MAX_PATH];
    _snprintf(file, sizeof(file), "%skeyhash.txt", path); file[sizeof(file) - 1] = 0;

    FILE* f = fopen(file, "rb");
    if (!f) return;
    char raw[64]; size_t got = fread(raw, 1, sizeof(raw) - 1, f); fclose(f);
    raw[got] = '\0';
    setKeyhashOverride(raw);   // validates + logs on reject
    if (g_haveKeyhashOverride) khLog("override loaded from keyhash.txt");
#endif
}

// ---- the hook --------------------------------------------------------------
// Overwrite field 1 (the MD5(cdkey) keyhash) in the gcd response buffer with the override.
// Same length (32), only when the field already holds 32 hex chars. SEH-guarded.
static void applyKeyhashOverride(char* output)
{
    if (!g_haveKeyhashOverride || !output) return;

    int result = 0;            // 0 = skipped, 1 = applied, 2 = not a hash field
    char before[40] = { 0 };
    __try {
        if (allHex(output, 32)) {            // field 1 = 32 hex (abuts the random at +0x20)
            memcpy(before, output, 32); before[32] = 0;
            memcpy(output, g_keyhashOverride, 32);   // same length -> safe
            result = 1;
        } else {
            result = 2;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = 2; }

    if (result == 1)      khLog("keyhash override applied: %s -> %s", before, g_keyhashOverride);
    else if (result == 2) khLog("gcd response fired but field 1 was not a 32-hex string; not applied");
}

// Capture the byte-exact local keyhash = field 1 = MD5(cdkey), BEFORE any override. This is
// the 100%-accurate value the server records; the capture's over-read keyhash is only a
// framing-shifted approximation of it. SEH-guarded.
static void captureLocalKeyhash(const char* output)
{
    __try {
        if (output && allHex(output, 32)) {
            memcpy(g_localKeyhash, output, 32); g_localKeyhash[32] = 0;
            if (!g_haveLocalKeyhash) khLog("local keyhash (byte-exact): %s", g_localKeyhash);
            g_haveLocalKeyhash = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Our detour for gcd_compute_response: build the real challenge response, capture the genuine
// field 1, then (optionally) swap field 1 for the override.
static void __fastcall gcdResponse_hook(char* cdkey, char* challenge, char* output)
{
    gcdResponse_orig(cdkey, challenge, output);
    captureLocalKeyhash(output);
    applyKeyhashOverride(output);
}

void installKeyhashHook()
{
#ifdef BFVPLUS_PUBLIC
    // Public release: no override can ever be configured, and this function already
    // bails out when there is none - so the whole thing is compiled out. That also
    // keeps the override's strings out of the shipped binary. g_haveLocalKeyhash
    // stays false, so capture.cpp simply never emits a LocalKeyHash record.
    return;
#else
    // ini was parsed by loadConfig() before us; try keyhash.txt if nothing was set there.
    loadKeyhashTxtFallback();

    if (!g_haveKeyhashOverride) {
        khLog("no keyhash override configured - hook not installed "
              "(set [keyhash] override in bfvplus.ini or create keyhash.txt)");
        return;
    }
    khLog("keyhash override active: %s", g_keyhashOverride);

    // Only patch the binary we reverse-engineered (BfVietnam.exe v1.21, base 0x400000).
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x00400000) {
        khLog("not BfVietnam.exe @0x400000 - hook skipped");
        return;
    }

    // Detour gcd_compute_response at its entry. Prologue is `sub esp,0x210`
    // (81 EC 10 02 00 00, 6 bytes, position-independent) - relocate all 6 into a trampoline,
    // then jmp the entry to our hook.
    uint8_t* fn = (uint8_t*)ADDR_gcdResponse;
    static const uint8_t expect[6] = { 0x81, 0xEC, 0x10, 0x02, 0x00, 0x00 };
    if (memcmp(fn, expect, 6) != 0) {
        khLog("gcd_compute_response prologue mismatch at 0x%08X - hook skipped", (unsigned)ADDR_gcdResponse);
        return;
    }

    // Trampoline: [6 relocated prologue bytes][E9 rel32 -> fn+6]. Calling it runs the real
    // function from after its prologue.
    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) { khLog("trampoline alloc failed"); return; }
    memcpy(tramp, fn, 6);
    tramp[6] = 0xE9;
    *(int32_t*)(tramp + 7) = (int32_t)((uintptr_t)(fn + 6) - ((uintptr_t)tramp + 11));
    FlushInstructionCache(GetCurrentProcess(), tramp, 16);
    gcdResponse_orig = (gcdResponse_t)tramp;

    // Patch the entry: E9 rel32 -> hook, padded with a NOP (5-byte jmp over the 6-byte prologue).
    DWORD oldProtect;
    if (!VirtualProtect(fn, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) { khLog("VirtualProtect failed"); return; }
    fn[0] = 0xE9;
    *(int32_t*)(fn + 1) = (int32_t)((uintptr_t)&gcdResponse_hook - ((uintptr_t)fn + 5));
    fn[5] = 0x90;
    VirtualProtect(fn, 6, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), fn, 6);
    khLog("keyhash hook installed at gcd_compute_response 0x%08X", (unsigned)ADDR_gcdResponse);
#endif
}
