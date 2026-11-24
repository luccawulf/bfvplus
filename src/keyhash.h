#pragma once

// playerKeyHash override for Battlefield Vietnam.
//
// On a valid local CD key the client builds a "playerKeyHash" property (the 32-char
// MD5 of your CD key) and sends it to the server; on an unpure server (CD-key checks
// off) that client-supplied value is exactly what the server logs. This module lets
// you present a chosen 32-hex hash instead, so you can test your own server with a
// known identity. Configure it in bfvplus.ini:
//
//   [keyhash]
//   override = 0123456789abcdef0123456789abcdef
//
// or drop a keyhash.txt (first 32 hex chars) next to BfVietnam.exe. See
// SSM Work/BFV_PORT_NOTES.md for the RE (builder FUN_0048e420, call site 0x0051FF7B).

// The validated 32-char override (NUL-terminated) and whether one is active.
extern char g_keyhashOverride[33];
extern bool g_haveKeyhashOverride;

// The local player's byte-exact keyhash (MD5 of the CD key), captured from field 1 of the
// gcd CD-key challenge response. 100% accurate - unlike the framing-shifted over-read in the
// capture. Empty until the client computes its challenge response on connect.
extern char g_localKeyhash[33];
extern bool g_haveLocalKeyhash;

// Set the override from a raw config string. Validates exactly 32 hex chars; ignores
// anything else. Called by config.cpp for [keyhash] override = ...
void setKeyhashOverride(const char* raw);

// Resolve the final override (ini value already applied; falls back to keyhash.txt)
// and, if one is active and the binary matches, patch the playerKeyHash build site to
// route through our thunk. Sanity-checks BfVietnam.exe v1.21 first; a no-op otherwise.
void installKeyhashHook();
