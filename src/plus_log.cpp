// bfvplus's own status log (see plus_log.h).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include "plus_log.h"

std::string plusExeDir()
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string();
    std::string s(path, n);
    size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : s.substr(0, slash + 1);
}

void plusLog(const char* fmt, ...)
{
    static std::mutex mu;
    static FILE* f = nullptr;
    static bool failed = false;
    std::lock_guard<std::mutex> lock(mu);
    if (failed) return;
    if (!f) {
        std::string dir = plusExeDir() + "logs";
        CreateDirectoryA(dir.c_str(), nullptr);
        f = std::fopen((dir + "\\bfvplus.log").c_str(), "a");
        if (!f) { failed = true; return; }
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fflush(f);
}
