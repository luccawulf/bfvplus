// The replay recorder's entry points, called from the existing hooks:
//   replay_onEvent     from capture.cpp's readEvent hook, once per GameEvent the server sends
//   replay_onInputPoll from sensitivity.cpp's mouse GetDeviceState/GetDeviceData hooks
//   replay_shutdown    from DllMain at process detach
//   replay_onFire      from fire_hook.cpp, once per round fireBarrel fires (stage 2: `f` lines)
// All three return at once when [replay] recordReplays is off.
#pragma once
#include <cstdint>

void replay_onEvent(void* ev);
void replay_onInputPoll();
void replay_shutdown();
void replay_onFire(uintptr_t fireArms, uintptr_t player, uintptr_t mat);
// From chat_hook.cpp, once per line the chat box is given: its sender's id (-1 none), his side and the line's
// UTF-16 units. Queued; written as a `chat` line by the next locked path.
void replay_onChat(int pid, int team, const uint16_t* units, size_t n);
