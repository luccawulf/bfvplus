// When replay files open and close, and when a sample is due (see replay_session.h).
#include "replay_session.h"
#include <cstdio>
#include <filesystem>
#include <system_error>
#include "replay_format.h"

namespace fs = std::filesystem;

static const uint64_t kSilentEndMs = 30000;

ReplaySession::ReplaySession(SessionConfig cfg) : cfg_(std::move(cfg))
{
    if (cfg_.hz < 1) cfg_.hz = 1;
    if (cfg_.hz > 30) cfg_.hz = 30;
}

void ReplaySession::onEvent(uint64_t nowMs, const std::tm& local, bool isServerInfo)
{
    if (!cfg_.enabled) return;
    if (active_ && isServerInfo) close();   // a new join: the previous file ends at its last activity
    if (!active_) openFile(nowMs, local);
    lastActivityMs_ = nowMs;
}

void ReplaySession::openFile(uint64_t nowMs, const std::tm& local)
{
    active_ = true;
    openMs_ = lastActivityMs_ = nextSampleMs_ = nowMs;

    std::error_code ec;
    fs::create_directories(cfg_.dir, ec);
    fs::path p;
    for (int dup = 0; dup < 1000; dup = dup == 0 ? 2 : dup + 1) {
        p = fs::path(cfg_.dir) / rf::fileName(local, dup);
        if (!fs::exists(p, ec)) break;
    }
    path_ = p.string();
    if (!file_.open(path_)) return;

    char iso[80];
    std::snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
        local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    file_.line(rf::headerLine({ cfg_.version, cfg_.mod, cfg_.build, iso, cfg_.hz }));
}

bool ReplaySession::onFrame(uint64_t nowMs, bool localPlayerPresent)
{
    if (!active_) return false;
    if (localPlayerPresent) lastActivityMs_ = nowMs;
    if (nowMs - lastActivityMs_ >= kSilentEndMs) {
        close();
        return false;
    }
    if (!file_.isOpen()) return false;
    file_.tick(nowMs);
    if (nowMs < nextSampleMs_) return false;
    const uint64_t period = 1000u / (uint64_t)cfg_.hz;
    while (nextSampleMs_ <= nowMs) nextSampleMs_ += period;
    return true;
}

void ReplaySession::write(std::string_view line)
{
    if (file_.isOpen()) file_.line(line);
}

double ReplaySession::t(uint64_t nowMs) const
{
    return nowMs >= openMs_ ? (double)(nowMs - openMs_) / 1000.0 : 0.0;
}

void ReplaySession::close()
{
    if (!active_) return;
    if (file_.isOpen()) {
        file_.line(rf::endLine(t(lastActivityMs_)));
        file_.close();
    }
    active_ = false;
}
