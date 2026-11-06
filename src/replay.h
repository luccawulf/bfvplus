// The replay recorder's entry points, called from the existing hooks:
//   replay_onEvent     from capture.cpp's readEvent hook, once per GameEvent the server sends
//   replay_onInputPoll from sensitivity.cpp's mouse GetDeviceState/GetDeviceData hooks
//   replay_shutdown    from DllMain at process detach
// All three return at once when [replay] recordReplays is off.
#pragma once

void replay_onEvent(void* ev);
void replay_onInputPoll();
void replay_shutdown();
