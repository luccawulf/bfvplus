#!/usr/bin/env python3
"""Check every patched site in src/bfv_addresses.h against one or more BfVietnam.exe files.

Usage: check_exes.py <exe> [<exe> ...]

Each SITE line names a virtual address and the bytes bfvplus expects there before it patches.
The address is mapped to a file offset through the exe's PE section table, and the bytes are
compared. Prints one row per site and one column per exe: ok, DIFF (with the bytes found), or
"none" when the address is outside every section. Exit 0 always; the table is the result.
"""
import hashlib
import pathlib
import re
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
TABLE = HERE.parent / "src" / "bfv_addresses.h"
SITE_RE = re.compile(r'^SITE\((\w+),\s*(0x[0-9A-Fa-f]+),\s*"([0-9A-Fa-f ]+)",\s*"([^"]*)"\)', re.M)


def sites():
    text = TABLE.read_text(encoding="utf-8")
    return [(m[1], int(m[2], 16), bytes.fromhex(m[3])) for m in SITE_RE.finditer(text)]


class Pe:
    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE file")
        nsect, optsize = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        self.sections = []
        for i in range(nsect):
            s = opt + optsize + 40 * i
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
            self.sections.append((va, max(vsize, rawsize), rawptr, rawsize))

    def read(self, va, n):
        rva = va - self.base
        for sva, size, rawptr, rawsize in self.sections:
            if sva <= rva < sva + size and rva - sva + n <= rawsize:
                off = rawptr + rva - sva
                return self.data[off:off + n]
        return None


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    table = sites()
    exes = []
    for p in paths:
        data = pathlib.Path(p).read_bytes()
        exes.append((pathlib.Path(p).name, hashlib.md5(data).hexdigest(), Pe(data)))
    for i, (name, md5, pe) in enumerate(exes):
        print(f"[{i}] {name}  md5 {md5}  base 0x{pe.base:08X}")
    print(f"{'site':24} {'va':10} " + " ".join(f"[{i}]".ljust(6) for i in range(len(exes))))
    for name, va, want in table:
        cells = []
        for _, _, pe in exes:
            got = pe.read(va, len(want))
            cells.append("none" if got is None else "ok" if got == want else "DIFF:" + got.hex(" "))
        print(f"{name:24} 0x{va:08X} " + " ".join(c.ljust(6) for c in cells))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
