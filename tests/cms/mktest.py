import re
src = open('../013/dlls/crypt32/tests/msg.c').read()
m = re.search(r'signedWithCertEmptyBareContent\[\] = \{(.*?)\};', src, re.S)
b = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', m.group(1)))
def der(tag, body):
    n = len(body)
    l = bytes([n]) if n < 0x80 else (bytes([0x81, n]) if n < 0x100 else bytes([0x82]) + n.to_bytes(2, 'big'))
    return bytes([tag]) + l + body
def parse(b):
    l, hl = b[1], 2
    if l & 0x80: hl += l & 0x7f; l = int.from_bytes(b[2:hl], 'big')
    return b[0], b[hl:hl + l], b[hl + l:]
def children(body):
    out = []
    while body: tag, c, body = parse(body); out.append((tag, c))
    return out
sd = children(parse(b)[1])
assert [t for t, _ in sd] == [0x02, 0x31, 0x30, 0xa0, 0x31]
cert = sd[3][1]
attr = bytes.fromhex('a1053003020101')
v2 = bytes.fromhex('a2053003020101')
items = [der(t, c) for t, c in sd]
items[0] = bytes.fromhex("020103")
items[3] = der(0xa0, attr + cert + v2)
out = der(0x30, b''.join(items))
open('attrcert_bare.der', 'wb').write(out)
# also as ContentInfo for cmsprobe
open('attrcert.der', 'wb').write(der(0x30, bytes.fromhex('06092a864886f70d010702') + der(0xa0, out)))
s = 'static const BYTE signedWithAttrCertsEmptyBareContent[] = {\n'
for i in range(0, len(out), 15):
    s += ','.join('0x%02x' % x for x in out[i:i + 15]) + (',\n' if i + 15 < len(out) else ' };\n')
print(s)
