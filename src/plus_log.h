// bfvplus's own status log: logs\bfvplus.log next to BfVietnam.exe, one timestamped line per
// call. Thread-safe. Used for "this part switched itself off" messages, never per frame.
#pragma once
#include <string>

void plusLog(const char* fmt, ...);

// The folder holding BfVietnam.exe, with a trailing backslash.
std::string plusExeDir();
