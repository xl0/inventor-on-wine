# 015 services.exe: ServicesPipeTimeout read as REG_SZ, 10 s default
Status: fixed (type only) · Owner: worker-014 · Branch: fix/014-services-session0 · Found in: inv-vm transplant — verify on real install path before fixing

`programs/services/services.c:load_registry_parameters()` only honours
`HKLM\System\CurrentControlSet\Control\ServicesPipeTimeout` when it is REG_SZ,
and defaults to 10000 ms. Windows documents it as REG_DWORD (ms) with a 30 s
default (per MS docs; unset on the VM). A REG_DWORD value set by an app or
admin is silently ignored; slow-starting services fail sooner than on Windows.
Noticed while debugging 014 (not its cause). WaitToKillServiceTimeout is
REG_SZ on the VM ("5000"), so only ServicesPipeTimeout's type is off.

Outcome: `services: Read ServicesPipeTimeout as REG_DWORD.` (REG_DWORD 2000
verified: sc start of a non-service fails after 2 s). The 10 s default is
left alone (30 s per docs would slow failing starts/tests; decide upstream).
