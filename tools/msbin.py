#!/usr/bin/env python3
"""Clean-room guard: flag Microsoft binaries.

Usage: msbin.py FILE...   prints "FILE<TAB>reason" for each Microsoft file;
exit 3 if any was flagged, else 0. A file is Microsoft if its version-info
CompanyName or an Authenticode cert O= names Microsoft. Used by decomp.sh
(refuses them) and transplant.sh (does not copy them).
"""
import struct, sys

def why(path):
    d = open(path, 'rb').read()
    r = []
    # VS_VERSIONINFO blocks: find CompanyName value.
    key = 'VS_VERSION_INFO'.encode('utf-16le')
    cn = 'CompanyName\0'.encode('utf-16le')
    i = d.find(key)
    while i >= 6:
        blk = d[i - 6:i - 6 + struct.unpack_from('<H', d, i - 6)[0]]
        j = blk.find(cn)
        if j >= 0:
            j += len(cn); j += -j % 4
            v = blk[j:].split(b'\0\0\0')[0] + b'\0'
            v = v[:len(v) & ~1].decode('utf-16le', 'replace')
            if 'microsoft' in v.lower():
                r.append('CompanyName ' + v.strip('\0'))
                break
        i = d.find(key, i + 1)
    # Authenticode: security data directory (index 4) -> cert blob.
    if d[:2] == b'MZ' and len(d) > 0x40:
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe + 4] == b'PE\0\0':
            opt = pe + 24
            dirs = opt + (96 if struct.unpack_from('<H', d, opt)[0] == 0x10b else 112)
            off, size = struct.unpack_from('<II', d, dirs + 4 * 8)
            # organizationName (2.5.4.10) == "Microsoft Corporation" in any cert of the signature
            blob = d[off:off + size] if size else b''
            if b'\x55\x04\x0a\x0c\x15Microsoft Corporation' in blob or \
               b'\x55\x04\x0a\x13\x15Microsoft Corporation' in blob:
                r.append('Authenticode cert O=Microsoft Corporation')
    return '; '.join(r)

if __name__ == '__main__':
    rc = 0
    for f in sys.argv[1:]:
        w = why(f)
        if w:
            print(f'{f}\t{w}'); rc = 3
    sys.exit(rc)
