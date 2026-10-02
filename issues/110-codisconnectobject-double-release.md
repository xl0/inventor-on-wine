# 110 CoDisconnectObject can drop the apartment reference twice → stub manager freed under a running call
Status: open (draft) · Owner: - · Branch: - · Found in: review of 109 (74c47dc99ea), also in upstream/base

## Symptom
A running or queued call holds a stub-manager reference, so after CoDisconnectObject the manager
stays findable. A second CoDisconnectObject (e.g. a "Close" method that disconnects itself, then
a teardown path disconnecting again), or a client's last release arriving during that call, takes
the call's reference: the manager is freed and dispatch_rpc's final release (rpc.c ~2165) hits
freed memory (RtlEnterCriticalSection). Stress: 1–8 crashes / 30 s, same rate on base and
integ; sometimes the server process dies.
Where: combase.c ~3351 (two stub_manager_int_release, no guard), stubmanager.c
stub_manager_ext_release → stub_manager_int_release.

## Windows (VM, tests/r109/disc_probe.c)
newcall: S_OK during the disconnected call; afterwards RPC_E_DISCONNECTED. double / rel: fine.

## Fix idea
Disconnect once: a flag so CoDisconnectObject drops the apartment ref only once, and external
releases after a disconnect don't drop internal refs; new calls still dispatch while one runs.
Repro: tests/r109/stress.c (`stress.exe run SECS`, R109_SAFE=0..3), disc_probe.c (modes newcall,
double, rel, relmd).
