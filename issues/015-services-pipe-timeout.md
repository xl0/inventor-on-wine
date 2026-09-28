# 015 services.exe: ServicesPipeTimeout read as REG_SZ, 10 s default
Status: open (draft, low) · Owner: - · Branch: - · Found in: inv-vm transplant — verify on real install path before fixing

`programs/services/services.c:load_registry_parameters()` only honours
`HKLM\System\CurrentControlSet\Control\ServicesPipeTimeout` when it is REG_SZ,
and defaults to 10000 ms. Windows documents it as REG_DWORD (ms) with a 30 s
default (per MS docs; unset on the VM). A REG_DWORD value set by an app or
admin is silently ignored; slow-starting services fail sooner than on Windows.
Noticed while debugging 014 (not its cause). WaitToKillServiceTimeout is
REG_SZ on the VM ("5000"), so only ServicesPipeTimeout's type is off.
