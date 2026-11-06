// Event records: one `{"k":"e",...}` line per GameEvent the server sends, named and laid out exactly
// as the BFV server recorder writes them (features/server-replay-recorder/src/core.c,
// emit_event_json, in the viewer repo), so the viewer reads a client file and a server file alike.
// The payload is the event object from +12; Session A showed the client's layout is the server's.
// Platform-independent (no windows.h).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class EventDecoder {
public:
    // Template id -> name (the client's ObjectTemplateManager, read by the caller); "" when unknown.
    using NameLookup = std::function<std::string(uint32_t templateId)>;
    explicit EventDecoder(NameLookup lookup = nullptr);

    // The line for one event; false when this type isn't written (or is a repeat the server
    // recorder also leaves out). `n` is how many payload bytes the caller copied: bytes past it read
    // as zero.
    bool line(int type, const uint8_t* payload, size_t n, double t, std::string& out);

private:
    NameLookup lookup_;
    uint32_t lastControl_[256] = {};
    uint32_t lastKit_[256] = {};
};

// A ServerInfo event (0x1a) is the first event of a join.
bool isServerInfo(int type);
