import gdb
def walk(head):
    n = 0; e = head['next']; h = head.address
    while e != h:
        n += 1; e = e['next']
    return n
apts = gdb.parse_and_eval('apts')
t = gdb.lookup_type('struct apartment').pointer()
e = apts['next']
while e != apts.address:
    a = e.cast(t)
    print("apt tid %d mta %d stubmgrs %d proxies %d" % (int(a['tid']), int(a['multi_threaded']), walk(a['stubmgrs']), walk(a['proxies'])))
    e = e['next']
