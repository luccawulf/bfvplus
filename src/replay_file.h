// One replay file: lines collect in memory and reach the disk only at a flush, once a second, so a
// game crash loses at most the last second (the lines flushed are in the OS's hands and survive the
// process) and the game thread never waits on the disk between flushes. Only whole lines are
// written, so a file cut by a crash ends on a whole record, just without the closing `end`; the
// viewer plays such a file to its last record (replay-recording.js takes the duration from the
// records and skips a line that isn't whole).
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
    bool create(const std::string& path); // the same, but fails rather than truncate an existing file
    void line(std::string_view s);        // buffer s + '\n'; a no-op when not open or failed
    void tick(uint64_t nowMs);            // flush when nowMs - lastFlushMs >= 1000
    void close();                         // flush and close
    bool isOpen() const { return file_ != nullptr; }
    bool failed() const { return failed_; }

private:
    bool openMode(const std::string& path, const char* mode);
    bool flush();
    std::FILE* file_ = nullptr;
    std::string buf_;
    uint64_t lastFlushMs_ = 0;
    bool failed_ = false;
};
