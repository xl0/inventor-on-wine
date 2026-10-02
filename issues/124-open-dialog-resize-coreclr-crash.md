# 124 Open dialog fails to repaint after Awesome resize; CoreCLR crash captured
Status: open (draft, Windows unchecked) · Owner: - · Branch: - · Found in: user's laptop, integ d7799da4d5

## Report and scope

During interactive Inventor use, the user was in the **Open dialog**:
- Resizing with **Awesome Mod+mouse**, not the dialog border, failed to repaint.
  Growing exposed black padding; shrinking cropped the old content and replaced
  areas with black padding.
- Clicking the path breadcrumbs did nothing.
- Inventor subsequently terminated with an unhandled access violation.

The user initially suspected DWG preview but is unsure. No particular DWG or
preview action is confirmed. The exact ordering relative to the fault is not
established: the dialog may already have stopped responding before either action.
**The repaint/navigation symptoms and the crash are not yet proven to share a
cause.** Split this report if investigation finds independent problems.

## Environment

Laptop, Ubuntu 24.04 x86_64; Wine-only, no local reference VM.
- `wine-11.18-494-gd7799da4d5`, new WoW64; native Inventor 2027.1 installation
  in `prefixes/inv`, Windows 11 setting, 144 DPI.
- Native .NET Framework 4.8 plus Autodesk's .NET 10.0.9 runtime.
- Awesome/X11, unified NVIDIA-primary desktop (RTX 3080 Laptop GPU),
  two monitors, one X screen.
- Picom: `--config /dev/null --backend glx --vsync --no-use-damage`.
- WineD3D Vulkan, no DXVK.
- `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--disable-gpu` retained from previous
  experiments; this has **not** been shown to disable all WebView2 GPU use.
- `WINEDEBUG=-all,err+all,+timestamp,+pid`.

32/64-bit console, caption GUI and COM proxy smoke checks passed on this build,
as did D3D11 Vulkan clear/present/readback and DComp interface queries.
That does not validate the application's Open dialog.

## Crash evidence

Two runs exited 5 on 2026-10-02:

1. Launch at 13:53:32, fatal log at approximately 14:20:55 (-0300).
   CoreCLR reports .NET 10.0.9 / CoreCLR 10.0.926.27113, unhandled `0xc0000005`,
   address `0x6fffc6156fef`, Wine thread `0020:03f8`. No managed stack was
   printed and no new dump was found in the user's Temp/CrashDumps directories.
2. Relaunch around 14:31 with GDB attached; fatal exception captured around
   14:35. Wine thread `04c4`, **".NET TP Worker"**. Fatal record:
   `0xc0000005`, flags 0, zero parameters, address `0x6fffc8106fef`
   = **`coreclr.dll+0x356fef`**. Capture succeeded; debugger detached and
   Inventor exited 5.

[Sanitized faulting-thread stack](attachments/124-open-dialog-crash-stack.txt).
The PE stack shows nested exception handling:

```text
NtRaiseException
RaiseFailFastException
coreclr.dll frames
call_seh_handler / call_seh_handlers / dispatch_exception
KiUserExceptionDispatcher
RtlLocateExtendedFeature2+0xb0
coreclr.dll frames
call_vectored_handler / dispatch_exception / KiUserExceptionDispatcher
unidentified address
```

`RtlLocateExtendedFeature2+0xb0` resolves in the matching Wine binary to
`dlls/ntdll/exception.c:855`, the read of `xs->CompactionMask`.
The final fatal record is from CoreCLR's reporting path; do not assume it
contains the original fault address or access parameters. Inspect the nested
exception context and extended-context inputs before attributing blame.
No Microsoft binary disassembly was performed; PE unwinding used `.pdata`.

An earlier `stub_manager_delete: Got page fault when releasing stub!` in the
first run belongs to **another process**, `0250:0278`, roughly three minutes
before the fatal Inventor record. It is not established as this crash's cause.
The older WPF key-tip FailFast is likewise a separate signature.

## Private evidence and capture method

These paths exist **on the laptop only**, not on the server or in git:
- First run: `inst/local/debug-20261002-135332-d7799da4d5/inventor.log`.
- Captured run: `inst/local/debug-20261002-142809-crashcapture/`.
  Contains `inventor.log`, `gdb.log`, `maps.txt`, `process.core`, `capture.gdb`,
  build version/settings and the `captured` completion marker.

GDB attached using `-iex "set sysroot /"` and a conditional breakpoint in the
native `ntdll.so` `NtRaiseException`, `first_chance == 0`. Wine's handled
signals were passed through. On the fatal exception it saved the record,
context, core, mappings, native stacks and PE stacks, then detached.
The mechanism was first verified using a controlled fail-fast probe.

The **ELF core** is sparse: 55 GiB logical, about 1.7 GiB allocated. It includes
anonymous heaps/stacks; unchanged file-backed mappings depend on the matching
local binaries. Preserve those binaries before rebuilding. The core, raw
registers and logs can contain model/account data: directory mode 0700, outputs
0600, git-ignored. Request targeted sanitized extracts from the laptop rather
than publishing the core.

## Task / next checks

1. Inspect the captured nested exception and `RtlLocateExtendedFeature2` inputs:
   valid `CONTEXT_EX`, XState offset/length and compaction configuration?
   Determine whether this is a Wine context bug, malformed caller input, or
   an earlier failure being mishandled.
2. Reproduce the Open-dialog symptoms using Awesome Mod+mouse resize; compare
   border-driven resize. [077](077-wm-move-no-sizemove.md)'s WM size/move change
   is included in this build and is relevant, but is **not** a proven regression.
   Check whether ENTER/EXITSIZEMOVE completes and the dialog resumes painting
   and breadcrumb navigation.
3. Establish whether the UI failure precedes the crash and whether DWG preview
   matters at all. Obtain Windows ground truth on the server's VM, respecting
   the shared licence-seat constraints.

No further interactive reproduction, Wine patch, registry workaround or
automatic relaunch has been applied after the captured crash.
