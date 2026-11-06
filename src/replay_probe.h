// The address-finding probe ([replay] probe = on; a developer switch, off by default). It writes
// raw bytes to logs\bfvplus_probe.log for an offline match against the lab server recorder's
// file of the same minutes: which thread calls what, each event type's bytes, the live
// soldiers, vehicles and players, and the object manager's tree. Read-only; SEH-guarded.
#pragma once

void probe_onEvent(void* ev);       // from the readEvent hook
void probe_onInputPoll(int kind);   // from the mouse hooks: 0 GetDeviceState, 1 GetDeviceData
