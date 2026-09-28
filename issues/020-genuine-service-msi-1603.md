# 020 Autodesk Genuine Service MSI returns 1603 (killBeacon step)
Status: open (draft, low) · Owner: - · Branch: - · Found in: Inventor install on integ 77645e2b221 (prefix inv)

## Observed
During the base product install the Autodesk Genuine Service MSI returned 1603,
failing in its `killBeacon` custom action. ODIS treated it as success and lists
the package INSTALLED; on the VM it installed cleanly. Logs: ODIS Install.log /
MSI log in prefixes/inv (see the install run of 2026-09-28 01:2x–01:32).

## Suspected (unverified)
killBeacon likely terminates/queries a process (taskkill/tasklist/WMI?) and
gets a result Wine formats or reports differently (cf. 007 tasklist).

## Task
Low priority (non-blocking). Identify the custom action and the failing call,
compare with the VM, fix if it's a Wine bug.
