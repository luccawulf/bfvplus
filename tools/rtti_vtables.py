#!/usr/bin/env python3
"""Find a class's vtables in BfVietnam.exe from its MSVC RTTI.

Usage: rtti_vtables.py <exe> <ClassName> [<ClassName> ...]
       rtti_vtables.py <exe> --like <substring>      (list RTTI class names containing it)

MSVC x86 RTTI: a TypeDescriptor is {type_info vtable ptr, spare, ".?AV<name>@<namespaces>@@"}.
A CompleteObjectLocator (COL) is {signature 0, offset, cdOffset, TypeDescriptor*, hierarchy*};
each vtable is preceded by a pointer to its COL. So: name -> TypeDescriptor -> COLs -> vtables.
The COL offset is the subobject's offset in the complete object (0 = the primary vtable).
"""
import pathlib
import re
import struct
import sys


class Pe:
    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsect = struct.unpack_from("<H", data, pe + 6)[0]
        optsize = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        self.sections = []
        for i in range(nsect):
            s = opt + optsize + 40 * i
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
            self.sections.append((va, vsize, rawptr, rawsize))

    def va_of(self, off):
        for va, vsize, rawptr, rawsize in self.sections:
            if rawptr <= off < rawptr + rawsize:
                return self.base + va + off - rawptr
        return None

    def offsets_of_dword(self, value):
        pat = struct.pack("<I", value)
        return [m.start() for m in re.finditer(re.escape(pat), self.data)]


def names_like(pe, sub):
    out = set()
    for m in re.finditer(rb"\.\?AV([A-Za-z0-9_@?$]+?)@@", pe.data):
        full = m.group(1).decode("latin-1")
        if sub.lower() in full.split("@")[0].lower():
            out.add(full)
    return sorted(out)


def vtables(pe, cls):
    results = []
    for m in re.finditer(rb"\.\?AV" + re.escape(cls.encode()) + rb"@[A-Za-z0-9_@]*@@\x00", pe.data):
        name = m.group(0)[:-1].decode("latin-1")
        td = pe.va_of(m.start() - 8)
        if td is None:
            continue
        for o in pe.offsets_of_dword(td):
            col_off = o - 12
            if col_off < 0 or struct.unpack_from("<I", pe.data, col_off)[0] != 0:
                continue
            offset, cdoffset = struct.unpack_from("<II", pe.data, col_off + 4)
            col = pe.va_of(col_off)
            if col is None:
                continue
            for p in pe.offsets_of_dword(col):
                vt = pe.va_of(p + 4)
                if vt is not None:
                    results.append((name, td, col, offset, vt))
    return results


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    pe = Pe(pathlib.Path(argv[0]).read_bytes())
    if argv[1] == "--like":
        for n in names_like(pe, argv[2]):
            print(n)
        return 0
    for cls in argv[1:]:
        found = vtables(pe, cls)
        if not found:
            print(f"{cls}: no RTTI")
        for name, td, col, offset, vt in found:
            print(f"{cls}: vtable 0x{vt:08X}  subobject +0x{offset:X}  COL 0x{col:08X}  TD 0x{td:08X}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
