// One replay file (see replay_file.h).
#include "replay_file.h"

bool ReplayFile::open(const std::string& path) { return openMode(path, "wb"); }

// "x": exclusive create (C11; MSVC's UCRT and glibc both take it), so a name chosen as free can't
// truncate a file that appeared meanwhile.
bool ReplayFile::create(const std::string& path) { return openMode(path, "wbx"); }

bool ReplayFile::openMode(const std::string& path, const char* mode)
{
    close();
    buf_.clear();
    lastFlushMs_ = 0;
    failed_ = false;
    file_ = std::fopen(path.c_str(), mode);
    if (!file_) {
        failed_ = true;
        return false;
    }
    return true;
}

void ReplayFile::line(std::string_view s)
{
    if (!file_) return;
    buf_.append(s.data(), s.size());
    buf_ += '\n';
}

void ReplayFile::tick(uint64_t nowMs)
{
    if (!file_ || nowMs - lastFlushMs_ < 2000) return;
    lastFlushMs_ = nowMs;
    flush();
}

// Write the buffer out. A short write or a failed fflush (a full disk) closes the file for good.
bool ReplayFile::flush()
{
    if (!file_) return false;
    if (!buf_.empty()) {
        size_t n = std::fwrite(buf_.data(), 1, buf_.size(), file_);
        bool ok = n == buf_.size() && std::fflush(file_) == 0;
        buf_.clear();
        if (!ok) {
            std::fclose(file_);
            file_ = nullptr;
            failed_ = true;
            return false;
        }
    }
    return true;
}

void ReplayFile::close()
{
    if (!file_) return;
    flush();
    if (file_) std::fclose(file_);
    file_ = nullptr;
}
