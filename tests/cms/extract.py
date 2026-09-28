import struct,sys
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
magic=struct.unpack_from('<H',d,pe+24)[0]
dd=pe+24+(112 if magic==0x20b else 96)
off,sz=struct.unpack_from('<II',d,dd+4*8)
p7=d[off+8:off+sz]
def hdr(b,i):
    t=b[i]; l=b[i+1]; i+=2
    if l&0x80:
        n=l&0x7f; l=int.from_bytes(b[i:i+n],'big'); i+=n
    return t,i,l
def walk(b,i,end,depth,path):
    while i<end:
        t,c,l=hdr(b,i)
        print('  '*depth+'%02x len %d @%d'%(t,l,i))
        if depth<7 and t&0x20: walk(b,c,c+l,depth+1,path)
        if t==0x06 and b[c:c+l]==bytes.fromhex('2b0601040182370303 01'.replace(' ','')):
            pass
        i=c+l
walk(p7,0,len(p7),0,[])
open('sig.p7','wb').write(p7)
