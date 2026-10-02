# 111 Last Release of a proxy during an in-flight call returns 0 (Windows 1)
Status: open (draft) · Owner: - · Branch: - · Found in: 110 worker (tests/r109/disc_probe.c `rel` mode)

Releasing a proxy's last reference while another thread is calling through it returns 0 on
Wine; Windows returns 1 (the call keeps the proxy referenced). The proxy may be torn down under
the call (unverified). Check combase proxy manager refcounting around in-flight calls
(ClientRpcChannelBuffer / interface proxies), match Windows, test in ole32:marshal.
