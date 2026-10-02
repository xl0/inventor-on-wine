# 115 Releasing a proxy to a dead in-process STA hangs
Status: open (draft; recheck on integ with 112) · Owner: - · Branch: - · Found in: 114 review (tests/r114/hol.c `hol.exe 4 0 2 1`)

STA A holds a proxy to STA B (same process); B uninitializes and its thread exits; A releases
the proxy → A hangs forever: ClientIdentity_Release → ifproxy_release_public_refs →
ClientRpcChannelBuffer_SendReceive → CoWaitForMultipleHandles; the server process never exits.
Seen on build 76edc4302b7 (before 112's "fail queued calls at STA uninit/exit"); may be fixed
by 112 — recheck first. Windows: the release should fail fast (RPC_E_DISCONNECTED/DIED).
