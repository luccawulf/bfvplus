// SEH-guarded reads of game memory for the replay recorder (Windows only). Every read returns false
// on a fault or an implausible address instead of crashing the game.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace seh {
bool ptrSane(uintptr_t p);                                     // 0x10000 .. 0x7FF00000
bool read(uintptr_t addr, void* dst, size_t n);
uint32_t u32(uintptr_t addr, bool* ok = nullptr);              // 0 on a fault
uint16_t u16(uintptr_t addr, bool* ok = nullptr);
bool vcall0(uintptr_t obj, unsigned slot, uintptr_t* result);  // __thiscall, no arguments
std::string msvcString(uintptr_t addr);                        // an MSVC 7.1 std::string; "" when implausible
}  // namespace seh
