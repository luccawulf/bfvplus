#pragma once

// SSM capture for Battlefield Vietnam — ported from bf42++ (BF1942). Hooks the
// received-GameEvent reader and logs every incoming event as JSONL to
// logs/bfvplus_server.log. See SSM Work/BFV_PORT_NOTES.md for the RE behind the
// addresses. Enabled via [capture] enabled in bfvplus.ini (default on).

extern bool g_captureEnabled;

// Object dump (vehicles/soldiers + template names). Toggled from bfvplus.ini
// [capture] dumpObjects = on/off (default on). When on, a snapshot is written on join
// and then every dumpObjectsInterval seconds (0 = only on join).
extern bool g_dumpObjects;
extern int  g_dumpObjectsInterval;

// Patch the single call site of the per-received-event reader (FUN_005b3a70) to
// route through our logger. Sanity-checks the binary first; a no-op if it doesn't
// match BfVietnam.exe v1.21.
bool installCaptureHook();   // false when the exe doesn't match (nothing patched)
