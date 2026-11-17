// The address-finding probe ([replay] probe = on; a developer switch, off by default). It writes raw
// bytes to logs\bfvplus_probe.log for an offline match against the lab server recorder's file of the
// same minutes. v1 (Session A) found stage 1's offsets; v2 (plan-1b, Session D) gathers stages 2-3:
// armour components, soldiers' states, control points, tickets, weapons and every round they fire,
// engines, moving parts, radios, the animation table and the recording player's own fields.
// Read-only (the fire hook only observes); SEH-guarded; nothing runs while the probe is off.
#pragma once
#include <cstdint>

void probe_onEvent(void* ev);       // from the readEvent hook
void probe_onInputPoll(int kind);   // from the mouse hooks: 0 GetDeviceState, 1 GetDeviceData

// One round from the fire hook (fire_hook.cpp), before fireBarrel runs: an `fb` line. Only while the
// probe is on.
void probe_onFire(uintptr_t fireArms, uintptr_t player, uintptr_t mat, int index);
