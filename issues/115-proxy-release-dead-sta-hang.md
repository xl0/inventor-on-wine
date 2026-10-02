# 115 Releasing a proxy to a dead in-process STA hangs
Status: fixed by 112 (no hang on integ 7e8ed9554cb) · Owner: - · Branch: - · Found in: 114 review (tests/r114/hol.c `hol.exe 4 0 2 1`)

STA A holds a proxy to STA B (same process); B uninitializes and its thread exits; A releases
the proxy → A hangs forever: ClientIdentity_Release → ifproxy_release_public_refs →
ClientRpcChannelBuffer_SendReceive → CoWaitForMultipleHandles; the server process never exits.
Seen on build 76edc4302b7 (before 112's "fail queued calls at STA uninit/exit"); may be fixed
by 112 — recheck first. Windows: the release should fail fast (RPC_E_DISCONNECTED/DIED).

Recheck 2026-10-02 on build 7e8ed9554cb (112 + 114): `hol.exe 4 0 2 1` x86_64 and i686 each exit normally
("server: exiting", rc 0, 0 fails, max latency <50 ms; scratch prefix, 60 s timeout). No hang.
