// When replay files open and close, and when a sample is due. Platform-independent: the caller
// passes the clock (ms) and the local time, so the rules are tested without the game.
//
// - A connection's file opens at its first event. While one is open, a ServerInfo event (the
//   join handshake; a server sends one per join) closes it and opens the next, so one file holds
//   one join.
// - A file closes after 30 s with neither an event nor the local player (a server that vanished
//   without a word), stamped at the last activity, once the local player has been seen in it; and
//   at game exit (close()).
// - A failed open is not retried until the next connection.
// - Samples fall due every 1000/hz ms on a fixed schedule from the open; a stall skips to the
//   next future slot instead of bursting.
#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include "replay_file.h"

struct SessionConfig { bool enabled; int hz; std::string dir, version, mod, build; };

class ReplaySession {
public:
    explicit ReplaySession(SessionConfig cfg);

    void onEvent(uint64_t nowMs, const std::tm& local, bool isServerInfo);
    bool onFrame(uint64_t nowMs, bool localPlayerPresent);   // true when a sample is due
    void write(std::string_view line);                       // into the open file; no-op otherwise
    double t(uint64_t nowMs) const;                          // seconds since the file opened
    void close(const char* reason = "exit");                 // end line at the last activity, close
    bool recording() const { return active_ && file_.isOpen(); }
    std::string path() const { return path_; }
    // Why the last file closed ("silent", "new join", "exit") and how many have, for the status log.
    const std::string& lastClose() const { return lastClose_; }
    int closeCount() const { return closeCount_; }
    double lastActivityT() const { return t(lastActivityMs_); }

private:
    void openFile(uint64_t nowMs, const std::tm& local);

    SessionConfig cfg_;
    ReplayFile file_;
    std::string path_;
    bool active_ = false;          // a file was attempted for the current connection
    bool localSeen_ = false;       // the local player has been present in this file
    uint64_t openMs_ = 0, lastActivityMs_ = 0, nextSampleMs_ = 0, lastDueMs_ = 0, lastWrittenMs_ = 0;
    std::string lastClose_;
    int closeCount_ = 0;
};
