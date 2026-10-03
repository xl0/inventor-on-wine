# 152 Inventor in the Linux VM (vmwl): who serves licensing, what a device is, and the startup crash
Status: closed for Wine (no Wine bug) · open: user decision on running Inventor in the guest · Owner: - · Branch: - · Found in: vmwl phase 2 (2026-10-03)

## Questions
(A) How does Inventor find its licensing service, and can the guest use the host's so that it is the same device?
(B) Why did Inventor crash in the guest 7 s after the licence checkout: 26.04 userland, the VM, or winewayland?

## A: licensing
- Discovery (AdskLicensingSDK_10.dll, `AdlSdkSocket.cpp` strings + decompile): the SDK reads
  `C:\ProgramData\Autodesk\AdskLicensingService\AdskLicensingService.data` = `{"Addr":"127.0.0.1:PORT"}` and connects
  `ws://host:port/...`; on failure it runs `AdskLicensingInstHelper.exe servicectl start`. Thin-client mode instead takes the
  address from the XML named by `AUTODESK_ADLM_THINCLIENT_ENV` (`ADSK_SERVICE_ADDRESS`). No registry value, pipe or fixed port.
- The service (Go, 12.13.1) logs `HTTP server running on <Addr>`: the saved address, or a new port + rewritten file when that
  one is busy. Log timelines: all prefixes started as copies with 39683 and moved to their own port on 09-28/29.
- Observed (inv4, `ss` every 50 ms over a start + checkout, 4170 samples): Inventor.exe, both AdskLicensingAgents and the
  agent's WebView2 network process connect to 127.0.0.1:46809 = inv4's own service; Inventor, the agents, AdskAccessCore and
  AdpSDKUtil also talk to inv4's AdskIdentityManager (two more loopback ports). Connections to 39683: none; inv-lic's
  service log has had no client since 2026-09-29. So inv-lic is not needed, and CODE.md's "one service serves all" was stale.
- Device ID = `H1&H2&H3` (service log `device ID ... for logon user`), produced by the agent the SDK starts on the client
  side (`MonitorEngine::generateDeviceId` -> `Autodesk::ADPDeviceIDSDK::GetDeviceId` in monitor.dll), each part SHA-256 of a
  UTF-16 string:
  H1 disk: WMI Win32_DiskDrive.SerialNumber of the boot disk; else `\\.\PhysicalDrive0` IOCTL_STORAGE_QUERY_PROPERTY;
  else `HKLM\HARDWARE\DEVICEMAP\Scsi\..\Identifier`; else the volume serial of the Windows drive.
  H2 machine: WMI Win32_ComputerSystemProduct.UUID; else SMBIOS (GetSystemFirmwareTable); else registry SystemUuid /
  ComputerHardwareId / `Cryptography\MachineGuid`. In Wine the UUID is the Unix machine-id.
  H3: hashed user name.
  Checked by hashing Wine's `wmic` output: host H1 = disk serial, host H2 = csproduct UUID; guest H2 = its csproduct UUID,
  guest H1 = volume serial (the virtio disk has no serial). All host prefixes log one ID; the guest copy logged another
  (H1, H2 differ) in each of its five starts, with the cached licence and a monitor connection each time.
- Consequence: routing the guest's SDK to a host service does not make it the same device (the agent runs in the guest), and
  would put the guest's ID into that host service. Not built; the unused guest forwarder (39683) is removed. Inventor is not
  to be started in the guest until the user decides how (or whether) to do it.

## B: the crash
- Dumps (5 guest runs, `tools/mdmp.py`): c0000005 reading 0 at `CommonUI.dll+0x60b90`, main thread, stack
  `FwWebBrowser.dll+0x330ee` <- `FwUI.dll+0x45865a` <- `Inventor.exe+0x71d0`.
- `CUIxMessageTerm::CUIxMessageTerm(L"InvWebBrowser")` calls through the static `m_pBuilder`, which is NULL. It is set by
  `FWxIFxMessageChannelBuilder::Initialize()`, called from the main frame's WM_CREATE handler (FwUI `FWxMainFrame::OnCreate`).
- Guest log, same thread: 1938 x `err:module:fixup_imports_ilonly mscoree.dll not found, IL-only binary L"System.Runtime.dll"
  cannot be loaded`, then `err:seh:user_callback_handler ignoring exception e0434352`: a CLR exception left WM_CREATE before
  the builder was set. mscoree was disabled by `WINEDLLOVERRIDES="mscoree,mshtml="`, exported for wineboot and inherited.
- A/B on the host (inv4, build/, X11 :101, NVIDIA Vulkan): without the override `invscen hello` passes; with it Inventor
  writes a dump 6 s after start with the same address, registers pattern and stack.

| where | driver | renderer | override | result |
|---|---|---|---|---|
| guest GNOME 50, sway | winewayland | gl (llvmpipe) | set | crash CommonUI+0x60b90 |
| host 22.04 | winex11 | vulkan (NVIDIA) | set | crash CommonUI+0x60b90 |
| host 22.04 | winex11 | vulkan (NVIDIA) | unset | runs (hello PASS) |
| host 22.04 mutter 42 | winewayland | gl (llvmpipe) | unset | runs (earlier pass, wayland.md) |
| guest, any | any | any | unset | not run (licensing) |

Root cause: launch environment, not Wine, 26.04, the VM or the Wayland driver. vmwl/README's recipe no longer exports the
override. Not shown: that Inventor runs on 26.04. What was checked there without Inventor: `d3d11_present` under
winewayland and winex11/Xwayland with gl and vulkan (all pass), and the build's library sonames (only ffmpeg 4 for
winedmo.so, pcsclite and odbc missing).
