// The replay file and session: buffering, flushing, failure, when files open and close, and the
// sample schedule. These tests use POSIX temp dirs and /dev/full, so they run on Linux only (the
// modules themselves are portable).
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "check.h"
#include "replay_file.h"
#include "replay_format.h"
#include "replay_session.h"

namespace fs = std::filesystem;

static std::string tempDir()
{
    char tmpl[] = "/tmp/bfvplus_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return d ? std::string(d) : std::string();
}

static std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static std::vector<std::string> linesOf(const std::string& path)
{
    std::vector<std::string> out;
    std::istringstream in(slurp(path));
    for (std::string l; std::getline(in, l);) out.push_back(l);
    return out;
}

static int fileCount(const std::string& dir)
{
    if (!fs::exists(dir)) return 0;
    int n = 0;
    for (auto& e : fs::directory_iterator(dir)) { (void)e; ++n; }
    return n;
}

static std::tm tm0()
{
    std::tm t{};
    t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 6; t.tm_hour = 21; t.tm_min = 30; t.tm_sec = 5;
    return t;
}

static SessionConfig cfg(const std::string& dir, bool enabled = true, int hz = 10)
{
    return SessionConfig{ enabled, hz, dir, "0.3-dev", "bfvietnam", "Nov  6 2026 21:30:00" };
}

TEST(file_buffers_until_2s)
{
    std::string dir = tempDir(), path = dir + "/x.ndjson";
    ReplayFile f;
    CHECK(f.open(path));
    f.line("a");
    f.line("b");
    f.tick(1999);
    CHECK_EQ(slurp(path), std::string(""));
    f.tick(2000);
    CHECK_EQ(slurp(path), std::string("a\nb\n"));
    f.close();
}

TEST(file_open_fails_cleanly)
{
    ReplayFile f;
    CHECK(!f.open("/nonexistent-dir/x.ndjson"));
    CHECK(f.failed());
    f.line("x");
    f.tick(5000);
    f.close();
    CHECK(!f.isOpen());
}

TEST(file_write_fails_cleanly)
{
    // /dev/full opens but every write fails with ENOSPC: a disk that fills mid-round.
    ReplayFile f;
    CHECK(f.open("/dev/full"));
    f.line(std::string(100000, 'x'));
    f.tick(2000);
    CHECK(f.failed());
    CHECK(!f.isOpen());
    f.line("more");
    f.tick(4000);
    f.close();
}

TEST(disabled_makes_nothing)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir, false));
    bool anyDue = false;
    for (int i = 0; i < 5; ++i) s.onEvent(1000 * i, tm0(), i == 0);
    for (int i = 0; i < 100; ++i) anyDue |= s.onFrame(16 * i, true);
    CHECK(!anyDue);
    CHECK(!s.recording());
    CHECK_EQ(fileCount(dir), 0);
}

TEST(opens_with_header)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(0, tm0(), true);
    CHECK(s.recording());
    CHECK_EQ(s.path(), (fs::path(dir) / "replay_20261106-213005.ndjson").string());
    s.close();
    auto lines = linesOf(s.path());
    CHECK(!lines.empty());
    if (!lines.empty())
        CHECK_EQ(lines[0], rf::headerLine({ "0.3-dev", "bfvietnam", "Nov  6 2026 21:30:00", "2026-11-06T21:30:05", 10 }));
}

TEST(ten_samples_per_second)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(0, tm0(), true);
    int due = 0;
    for (uint64_t t = 0; t <= 999; t += 16) due += s.onFrame(t, true) ? 1 : 0;
    CHECK_EQ(due, 10);
}

TEST(no_burst_after_stall)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(0, tm0(), true);
    for (uint64_t t = 0; t < 100; t += 16) s.onFrame(t, true);
    CHECK(s.onFrame(950, true));
    CHECK(!s.onFrame(966, true));
}

TEST(same_second_second_file)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(0, tm0(), true);
    std::string first = s.path();
    s.onEvent(500, tm0(), true);
    CHECK_EQ(first, (fs::path(dir) / "replay_20261106-213005.ndjson").string());
    CHECK_EQ(s.path(), (fs::path(dir) / "replay_20261106-213005-2.ndjson").string());
    s.close();
    auto lines = linesOf(first);
    CHECK(!lines.empty());
    if (!lines.empty()) CHECK_EQ(lines.back().rfind("{\"k\":\"end\"", 0), size_t(0));
    CHECK_EQ(fileCount(dir), 2);
}

TEST(silent_end_after_30s)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(1000, tm0(), true);
    std::string first = s.path();
    s.onFrame(1000, true);   // the local player seen once: the 30 s rule is armed
    for (uint64_t t = 1000; t <= 31000; t += 100) s.onFrame(t, false);
    CHECK(!s.recording());
    CHECK_EQ(s.lastClose(), std::string("silent"));
    auto lines = linesOf(first);
    if (!lines.empty()) CHECK_EQ(lines.back(), std::string("{\"k\":\"end\",\"t\":0.000}"));
    std::tm later = tm0(); later.tm_sec = 40;
    s.onEvent(40000, later, false);
    CHECK(s.recording());
    CHECK(s.path() != first);
}

TEST(local_player_keeps_file_open)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(1000, tm0(), true);
    s.onFrame(20000, true);
    for (uint64_t t = 20100; t <= 31000; t += 100) s.onFrame(t, false);
    CHECK(s.recording());
    for (uint64_t t = 31100; t <= 50000; t += 100) s.onFrame(t, false);
    CHECK(!s.recording());
    auto lines = linesOf(s.path());
    if (!lines.empty()) CHECK_EQ(lines.back(), std::string("{\"k\":\"end\",\"t\":19.000}"));
}

TEST(silent_end_waits_for_local_player)
{
    // During a join the local player doesn't exist until the database ends, and a slow map load can
    // pass 30 s with only polls: the rule arms only once the local player has been seen in the file.
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    s.onEvent(0, tm0(), true);
    for (uint64_t t = 0; t <= 40000; t += 100) s.onFrame(t, false);
    CHECK(s.recording());
    s.onFrame(41000, true);
    for (uint64_t t = 41100; t <= 71000; t += 100) s.onFrame(t, false);
    CHECK(!s.recording());
    auto lines = linesOf(s.path());
    if (!lines.empty()) CHECK_EQ(lines.back(), std::string("{\"k\":\"end\",\"t\":41.000}"));
}

TEST(close_reasons)
{
    std::string dir = tempDir() + "/replays";
    ReplaySession s(cfg(dir));
    CHECK_EQ(s.closeCount(), 0);
    s.onEvent(0, tm0(), true);
    s.onEvent(500, tm0(), true);   // a second join
    CHECK_EQ(s.closeCount(), 1);
    CHECK_EQ(s.lastClose(), std::string("new join"));
    s.close();
    CHECK_EQ(s.closeCount(), 2);
    CHECK_EQ(s.lastClose(), std::string("exit"));
    s.close();                     // nothing open: no second count
    CHECK_EQ(s.closeCount(), 2);
}

TEST(failed_open_not_retried_every_event)
{
    std::string base = tempDir(), dir = base + "/replays";
    { std::ofstream blocker(dir); blocker << "x"; }   // a file where the folder should go
    ReplaySession s(cfg(dir));
    s.onEvent(0, tm0(), true);
    CHECK(!s.recording());
    fs::remove(dir);
    s.onEvent(100, tm0(), false);                      // same connection: no new attempt
    CHECK(!s.recording());
    CHECK_EQ(fileCount(dir), 0);
    s.onEvent(200, tm0(), true);                       // a new connection tries again
    CHECK(s.recording());
}
