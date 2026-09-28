"""Mutate the RFC 3161 token inside a signed PKCS#7 (p7x with PKCX magic, or PE exe). Usage: mutate.py IN OUT VARIANT"""
import sys, struct
def der(tag, body):
    n = len(body)
    if n < 0x80: l = bytes([n])
    else:
        b = n.to_bytes((n.bit_length() + 7) // 8, 'big'); l = bytes([0x80 | len(b)]) + b
    return bytes([tag]) + l + body
def parse(b):
    l, hl = b[1], 2
    if l & 0x80: hl += l & 0x7f; l = int.from_bytes(b[2:hl], 'big')
    return b[0], b[hl:hl + l], b[hl + l:]
def children(body):
    out = []
    while body: tag, c, body = parse(body); out.append([tag, c])
    return out
TSOID = bytes.fromhex('060a2b0601040182370303 01'.replace(' ', ''))
def fix_token(tok, variant):
    ci = children(parse(tok)[1])
    sd = children(children(ci[1][1])[0][1])
    tags = [t for t, _ in sd]
    junk = bytes.fromhex('3003020100')
    certs_i = tags.index(0xa0)
    if variant == 'junkcrl':
        sd.insert(certs_i + 1, [0xa1, junk])
    elif variant == 'junkcert':
        sd[certs_i][1] = sd[certs_i][1] + junk
    elif variant == 'attr':
        sd[certs_i][1] = bytes.fromhex('a1053003020101') + sd[certs_i][1]
    elif variant == 'same':
        pass
    return der(0x30, der(*ci[0]) + der(0xa0, der(0x30, b''.join(der(t, c) for t, c in sd))))
def fix_p7(p7, variant):
    ci = children(parse(p7)[1])
    sd = children(children(ci[1][1])[0][1])
    sis = children(sd[-1][1])
    si = children(sis[0][1])
    assert si[-1][0] == 0xa1
    attrs = children(si[-1][1])
    for a in attrs:
        ac = children(a[1])
        if der(*ac[0]) == TSOID:
            vals = children(ac[1][1])
            ac[1][1] = der(0x30, fix_token_body(vals[0][1], variant))
            a[1] = b''.join(der(t, c) for t, c in ac)
    si[-1][1] = b''.join(der(t, c) for t, c in attrs)
    sis[0][1] = b''.join(der(t, c) for t, c in si)
    sd[-1][1] = b''.join(der(t, c) for t, c in sis)
    return der(0x30, der(*ci[0]) + der(0xa0, der(0x30, b''.join(der(t, c) for t, c in sd))))
def fix_token_body(body, variant):
    return der(0x30, body)[0:0] + fix_token(der(0x30, body), variant)[len(der(0x30, b'')) - 2:] if False else parse(fix_token(der(0x30, body), variant))[1]
d = open(sys.argv[1], 'rb').read()
v = sys.argv[3]
if d[:4] == b'PKCX':
    out = b'PKCX' + fix_p7(d[4:], v)
else:
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    dd = pe + 24 + (112 if struct.unpack_from('<H', d, pe + 24)[0] == 0x20b else 96) + 32
    off, sz = struct.unpack_from('<II', d, dd)
    assert off + sz == len(d)
    p7 = fix_p7(parse(d[off + 8:])[1] and d[off + 8:off + 8 + len(der(0x30, parse(d[off + 8:])[1]))], v)
    p7 += b'\0' * (-len(p7) % 8)
    ent = struct.pack('<IHH', 8 + len(p7), 0x200, 2) + p7
    out = bytearray(d[:off] + ent)
    struct.pack_into('<II', out, dd, off, len(ent))
    out = bytes(out)
open(sys.argv[2], 'wb').write(out)
