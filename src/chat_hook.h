// The chat box's lines for the replay: every player line BfMenu::addPlayerChatMessage (0x004CC2A0) puts
// in the chat box, the recording player's own among them (the server never sends him his own line back,
// so the ChatFragment pieces miss it), queued for the recorder's `chat` lines (replay_onChat). Installed
// with [replay] recordReplays on, and only when the function's entry and all seven of its call sites match
// (it logs which one did not otherwise).
#pragma once

bool installChatHook();
// Whether all seven call sites are hooked: a file then says it has the chat box's lines (its header's
// `chatLines`), and the viewer reads them alone.
bool chatHookInstalled();
