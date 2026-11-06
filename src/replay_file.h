// One replay file: lines collect in memory and reach the disk only at a flush, at most every
// 2 s, so a crash loses at most 2 s and the game thread never waits on the disk between flushes.
// A file that can't be created or written stops quietly (failed() == true): the game carries on.
// Platform-independent (no windows.h).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

class ReplayFile {
public:
    ReplayFile() = default;
    ReplayFile(const ReplayFile&) = delete;
    ReplayFile& operator=(const ReplayFile&) = delete;
    ~ReplayFile() { close(); }

    bool open(const std::string& path);   // false (and failed() true) when it can't be created
    void line(std::string_view s);        // buffer s + '\n'; a no-op when not open or failed
    void tick(uint64_t nowMs);            // flush when nowMs - lastFlushMs >= 2000
    void close();                         // flush and close
    bool isOpen() const { return file_ != nullptr; }
    bool failed() const { return failed_; }

private:
    bool flush();
    std::FILE* file_ = nullptr;
    std::string buf_;
    uint64_t lastFlushMs_ = 0;
    bool failed_ = false;
};
