// SEH-guarded reads (see seh_read.h). The functions holding __try have no C++ objects (MSVC C2712).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include "seh_read.h"

namespace seh {

bool ptrSane(uintptr_t p) { return p >= 0x00010000 && p < 0x7FF00000; }

static bool inImage(uintptr_t p) { return p >= 0x00400000 && p < 0x01400000; }

bool read(uintptr_t addr, void* dst, size_t n)
{
    if (!ptrSane(addr)) return false;
    __try { std::memcpy(dst, (const void*)addr, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t u32(uintptr_t addr, bool* ok)
{
    uint32_t v = 0;
    bool r = read(addr, &v, 4);
    if (ok) *ok = r;
    return r ? v : 0;
}

uint16_t u16(uintptr_t addr, bool* ok)
{
    uint16_t v = 0;
    bool r = read(addr, &v, 2);
    if (ok) *ok = r;
    return r ? v : 0;
}

bool vcall0(uintptr_t obj, unsigned slot, uintptr_t* result)
{
    if (!ptrSane(obj)) return false;
    __try {
        uintptr_t vt = *(const uintptr_t*)obj;
        if (!inImage(vt)) return false;
        uintptr_t fn = *(const uintptr_t*)(vt + 4 * slot);
        if (!inImage(fn)) return false;
        *result = (uintptr_t)((void*(__thiscall*)(void*))fn)((void*)obj);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string msvcString(uintptr_t addr)
{
    uint32_t size = u32(addr + 0x10), cap = u32(addr + 0x14);
    if (size == 0 || size > 200 || cap < size) return std::string();
    uintptr_t chars = cap < 16 ? addr : u32(addr);
    char buf[201];
    if (!read(chars, buf, size)) return std::string();
    return std::string(buf, size);
}

}  // namespace seh
