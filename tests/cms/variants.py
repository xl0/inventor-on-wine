import sys
sys.path.insert(0, '.')
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
    while body: tag, c, body = parse(body); out.append((tag, c))
    return out
p7 = open('sig.p7', 'rb').read()
tok = p7[4086:4086 + 4 + 6040]
open('token.der', 'wb').write(tok)
ci = children(parse(tok)[1])
sd = children(children(ci[1][1])[0][1])
print([hex(t) for t, _ in sd])
certs = children(sd[3][1])
print([hex(t) for t, _ in certs])
X1, X2, A = [der(t, c) for t, c in certs]
def build(certs_field, crls=None):
    items = [der(t, c) for t, c in sd[:3]]
    if certs_field is not None: items.append(der(0xa0, certs_field))
    if crls is not None: items.append(der(0xa1, crls))
    items.append(der(*sd[4]))
    return der(0x30, der(*ci[0]) + der(0xa0, der(0x30, b''.join(items))))
assert build(X1 + X2 + A) == tok
junk = bytes.fromhex('3003020100')
v = {
 'mid': build(X1 + A + X2),
 'first': build(A + X1 + X2),
 'v2': build(X1 + X2 + der(0xa2, parse(A)[1])),
 'other': build(X1 + X2 + der(0xa3, parse(A)[1])),
 'prim1': build(X1 + X2 + der(0x81, b'\x01\x02')),
 'junkcert': build(X1 + junk + X2),
 'onlyattr': build(A),
 'noattr': build(X1 + X2),
 'junkcrl': build(X1 + X2, junk),
 'attrcrl': build(X1 + X2, A),
}
for k, b in v.items(): open('tok_%s.der' % k, 'wb').write(b)

Ac = children(parse(A)[1])
print('attr parts', [hex(t) for t, _ in Ac])
signed_min = bytes.fromhex('3003020101' '30020600' '030100')
w = {
 'A1min': der(0xa1, signed_min), 'A2min': der(0xa2, signed_min), 'A3min': der(0xa3, signed_min),
 'A0min': der(0xa0, signed_min), 'A1tiny': der(0xa1, bytes.fromhex('3003020101')),
 'A1trunc': der(0xa1, der(*Ac[0])), 'int': bytes.fromhex('020101'), 'prim81': bytes.fromhex('81020102'),
 'A1realinfo': der(0xa1, der(*Ac[0]) + bytes.fromhex('30020600030100')),
 'A1sha': der(0xa1, bytes.fromhex('3003020101') + der(*Ac[1]) + bytes.fromhex('030100')),
 'A1realsigmin': der(0xa1, bytes.fromhex('3003020101') + der(*Ac[1]) + der(*Ac[2])),
 'A1empty': bytes.fromhex('a100'),
 'A0realX': der(0xa0, parse(X1)[1]),
 'A1realX': der(0xa1, parse(X1)[1]),
}
for k, e in w.items(): open('w_%s.der' % k, 'wb').write(build(X1 + X2 + e))
