#!/usr/bin/env python3
"""Generate a synthetic AppxSignature.p7x for the wintrust test_wintrust_blob (issues 008, 009).

Own RSA root, code-signing leaf valid only in 2000, TSA cert; the PKCS #7
Authenticode signature carries an RFC 3161 time-stamp token (genTime
2000-06-01) as szOID_RFC3161_counterSign. Writes p7x_ts.p7x, p7x_nots.p7x
and p7x.h (C arrays), plus bad-token variants p7x_{badimprint,badsig,noeku}.p7x.
Usage: p7x_gen.py [REAL.p7x] -- with a real (trusted) p7x also writes copies
with its token replaced by ours: real_ourtsa{,_root,_noeku,_badimprint}.p7x
(genTime 2026-02-10; _root also puts our untrusted root in the token).
Needs python3-cryptography.
"""
import datetime, hashlib, sys
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, padding

def der(tag, body):
    n = len(body)
    if n < 0x80: l = bytes([n])
    else:
        b = n.to_bytes((n.bit_length() + 7) // 8, 'big'); l = bytes([0x80 | len(b)]) + b
    return bytes([tag]) + l + body
def seq(*a): return der(0x30, b''.join(a))
def set_(*a): return der(0x31, b''.join(sorted(a)))
def ctx(n, body): return der(0xa0 + n, body)
def integer(v): return der(0x02, v.to_bytes(v.bit_length() // 8 + 1, 'big'))
def octets(b): return der(0x04, b)
def oid(s):
    p = [int(x) for x in s.split('.')]; out = bytes([40 * p[0] + p[1]])
    for v in p[2:]:
        enc = [v & 0x7f]; v >>= 7
        while v: enc.append(0x80 | (v & 0x7f)); v >>= 7
        out += bytes(reversed(enc))
    return der(0x06, out)
NULL = b'\x05\x00'
SHA256 = seq(oid('2.16.840.1.101.3.4.2.1'), NULL)
RSA = seq(oid('1.2.840.113549.1.1.1'), NULL)
def attr(o, *vals): return seq(oid(o), set_(*vals))
def contents(e): return e[2:] if e[1] < 0x80 else e[2 + (e[1] & 0x7f):]

def mkcert(cn, key, issuer, ikey, nb, na, ca=False, eku=None):
    b = (x509.CertificateBuilder()
         .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)]))
         .issuer_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, issuer)]))
         .public_key(key.public_key()).serial_number(len(cn))
         .not_valid_before(nb).not_valid_after(na)
         .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True))
    if eku: b = b.add_extension(x509.ExtendedKeyUsage([eku]), critical=True)
    return b.sign(ikey, hashes.SHA256())

def signer_info(cert, key, attrs):
    signed = set_(*attrs)
    sig = key.sign(signed, padding.PKCS1v15(), hashes.SHA256())
    ias = seq(cert.issuer.public_bytes(), integer(cert.serial_number))
    return sig, [integer(1), ias, SHA256, der(0xa0, contents(signed)), RSA, octets(sig)]

D = datetime.datetime
key = [rsa.generate_private_key(65537, 2048) for _ in range(3)]
root = mkcert('Wine Test P7x Root', key[0], 'Wine Test P7x Root', key[0], D(2000, 1, 1), D(2050, 1, 1), ca=True)
leaf = mkcert('Wine Test P7x Signer', key[1], 'Wine Test P7x Root', key[0], D(2000, 1, 1), D(2001, 1, 1),
              eku=ExtendedKeyUsageOID.CODE_SIGNING)
tsa = mkcert('Wine Test P7x TSA', key[2], 'Wine Test P7x Root', key[0], D(2000, 1, 1), D(2050, 1, 1),
             eku=ExtendedKeyUsageOID.TIME_STAMPING)
cder = lambda c: c.public_bytes(serialization.Encoding.DER)

# SpcIndirectDataContent { SpcSipInfo (as in real .appx/.msix signatures), APPX digest }
sipinfo = seq(integer(0x01010000), octets(bytes.fromhex('4BDFC50A07CEE24DB76E23C839A09FD1')), *[integer(0)] * 5)
digest = b'APPX' + b''.join(t + bytes(32) for t in (b'AXPC', b'AXCD', b'AXCT', b'AXBM', b'AXCI'))
indirect = seq(seq(oid('1.3.6.1.4.1.311.2.1.30'), sipinfo), seq(SHA256, octets(digest)))
# Authenticode hashes the content octets of the SpcIndirectDataContent
sig, si = signer_info(leaf, key[1], [attr('1.2.840.113549.1.9.3', oid('1.3.6.1.4.1.311.2.1.4')),
                                     attr('1.2.840.113549.1.9.4', octets(hashlib.sha256(contents(indirect)).digest()))])

tsa_noeku = mkcert('Wine Test P7x TSA', key[2], 'Wine Test P7x Root', key[0], D(2000, 1, 1), D(2050, 1, 1))

def mktoken(sig, imprint_ok=True, sig_ok=True, cert=tsa, gentime=b'20000601120000Z', extra_certs=b''):
    imprint = hashlib.sha256(sig + (b'' if imprint_ok else b'x')).digest()
    tstinfo = seq(integer(1), oid('1.2.3.4'), seq(SHA256, octets(imprint)), integer(42), der(0x18, gentime))
    tsig, tsi = signer_info(cert, key[2], [
        attr('1.2.840.113549.1.9.3', oid('1.2.840.113549.1.9.16.1.4')),
        attr('1.2.840.113549.1.9.4', octets(hashlib.sha256(tstinfo).digest())),
        attr('1.2.840.113549.1.9.16.2.47', seq(seq(seq(octets(hashlib.sha256(cder(cert)).digest())))))])
    if not sig_ok: tsi[-1] = octets(bytes([tsig[0] ^ 1]) + tsig[1:])
    return seq(oid('1.2.840.113549.1.7.2'), ctx(0, seq(
        integer(3), set_(SHA256), seq(oid('1.2.840.113549.1.9.16.1.4'), ctx(0, octets(tstinfo))),
        ctx(0, cder(cert) + extra_certs), set_(seq(*tsi)))))

tokens = {'ts': mktoken(sig), 'badimprint': mktoken(sig, imprint_ok=False), 'badsig': mktoken(sig, sig_ok=False),
          'noeku': mktoken(sig, cert=tsa_noeku)}

def p7x(ts):
    s = si + ([der(0xa1, contents(set_(attr('1.3.6.1.4.1.311.3.3.1', tokens[ts]))))] if ts else [])
    msg = seq(oid('1.2.840.113549.1.7.2'), ctx(0, seq(
        integer(1), set_(SHA256), seq(oid('1.3.6.1.4.1.311.2.1.4'), ctx(0, indirect)),
        ctx(0, cder(leaf) + cder(root)), set_(seq(*s)))))
    return b'PKCX' + msg

h = open('p7x.h', 'w')
for name, ts in (('p7x_ts', 'ts'), ('p7x_nots', None)):
    data = p7x(ts)
    open(name + '.p7x', 'wb').write(data)
    h.write('static const BYTE %s[] =\n{\n' % name)
    for i in range(0, len(data), 16):
        h.write('    ' + ','.join('0x%02x' % b for b in data[i:i + 16]) + ',\n')
    h.write('};\n')
for ts in ('badimprint', 'badsig', 'noeku'):
    open('p7x_%s.p7x' % ts, 'wb').write(p7x(ts))

def parse(b):  # -> (tag, contents, rest)
    l, hl = b[1], 2
    if l & 0x80: hl += l & 0x7f; l = int.from_bytes(b[2:hl], 'big')
    return b[0], b[hl:hl + l], b[hl + l:]
def children(body):
    out = []
    while body: tag, c, body = parse(body); out.append((tag, c))
    return out
def replace_token(p7s, mk):
    # ContentInfo { oid, [0] SignedData { ..., signerInfos SET { SignerInfo { ..., sig, [1] unauthAttrs } } } }
    ci = children(parse(p7s)[1])
    sd = children(children(ci[1][1])[0][1])
    si = children(children(sd[-1][1])[0][1])
    attrs = [seq(oid('1.3.6.1.4.1.311.3.3.1'), set_(mk(si[-2][1])))
             if children(a)[0][1] == oid('1.3.6.1.4.1.311.3.3.1')[2:] else der(t, a) for t, a in children(si[-1][1])]
    si = [der(t, c) for t, c in si[:-1]] + [der(0xa1, b''.join(attrs))]
    sd = [der(t, c) for t, c in sd[:-1]] + [set_(seq(*si))]
    return seq(der(*ci[0]), ctx(0, seq(*sd)))

if len(sys.argv) > 1:
    real = open(sys.argv[1], 'rb').read()
    gt = b'20260210120000Z'
    for name, mk in (('real_ourtsa', lambda s: mktoken(s, gentime=gt)),
                     ('real_ourtsa_root', lambda s: mktoken(s, gentime=gt, extra_certs=cder(root))),
                     ('real_ourtsa_noeku', lambda s: mktoken(s, gentime=gt, cert=tsa_noeku)),
                     ('real_ourtsa_badimprint', lambda s: mktoken(s, gentime=gt, imprint_ok=False))):
        open(name + '.p7x', 'wb').write(b'PKCX' + replace_token(real[4:], mk))
