#!/usr/bin/env python3
# Nearest public symbol per RVA from a Microsoft PDB (MSF 7.0; S_PUB32 only): tools/pdbpub.py FILE.pdb RVA... (hex).
# PDB of a module: RSDS GUID+age in its PE debug directory -> msdl.microsoft.com/download/symbols/NAME.pdb/<GUID><age>/NAME.pdb
# (reading published symbols is not disassembly). Used for mfc140u frames in 164.
import struct, sys, bisect
d = open(sys.argv[1], 'rb').read()
bs, fpm, nblocks, dirsz, _, dirmap = struct.unpack_from('<IIIIII', d, 32)
ndir = -(-dirsz // bs)
dirblocks = struct.unpack_from('<%dI' % ndir, d, dirmap * bs)
dir_ = b''.join(d[b * bs:(b + 1) * bs] for b in dirblocks)[:dirsz]
ns = struct.unpack_from('<I', dir_, 0)[0]
sizes = struct.unpack_from('<%dI' % ns, dir_, 4)
pos = 4 + 4 * ns; streams = []
for s in sizes:
    n = 0 if s == 0xffffffff else -(-s // bs)
    streams.append((s, struct.unpack_from('<%dI' % n, dir_, pos))); pos += 4 * n
def stream(i):
    s, bl = streams[i]
    return b''.join(d[b * bs:(b + 1) * bs] for b in bl)[:s]
dbi = stream(3)
# DBI header: sym record stream at +20 (u16); optional debug header gives the section header stream
gs, ver, age, gsi, bn, psi, pdbver, symrec = struct.unpack_from('<iIIHHHHH', dbi, 0)
modsz, seccsz, secmapsz, srcsz, tssz, mfc, dbghdrsz, ecsz = struct.unpack_from('<iiiiiIii', dbi, 24)
dbgoff = 64 + modsz + seccsz + secmapsz + srcsz + tssz + ecsz
dbg = struct.unpack_from('<%dH' % (dbghdrsz // 2), dbi, dbgoff)
sechdr = stream(dbg[5])
secs = [struct.unpack_from('<II', sechdr, 40 * i + 8) for i in range(len(sechdr) // 40)]  # vsize, va
sym = stream(symrec); o = 0; pubs = []
while o + 4 <= len(sym):
    ln, kind = struct.unpack_from('<HH', sym, o)
    if kind == 0x110e:  # S_PUB32
        flags, off, seg = struct.unpack_from('<IIH', sym, o + 4)
        name = sym[o + 14:sym.index(b'\0', o + 14)].decode('latin1')
        if 0 < seg <= len(secs): pubs.append((secs[seg - 1][1] + off, name))
    o += ln + 2
pubs.sort(); keys = [p[0] for p in pubs]
for a in sys.argv[2:]:
    rva = int(a, 16); i = bisect.bisect_right(keys, rva) - 1
    print('%#x  %s + %#x' % (rva, pubs[i][1], rva - pubs[i][0]))
