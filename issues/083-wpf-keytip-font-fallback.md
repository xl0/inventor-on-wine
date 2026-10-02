# 083 Inventor ribbon key tips crash in WPF font fallback
Status: open (draft; corefonts installed, exact UI trigger/fix unverified) · Owner: - · Branch: - · Found in: local Inventor 2027.1

## Symptom
On the local workstation, `integ 38e4c1c00c`
(`wine-11.18-367-g38e4c1c00c`), Inventor exits with status 35.
This is distinct from the old-build `SetWindowSubclass` crash (046).
The application requests `Environment.FailFast("Unrecoverable system error.")`
while rendering ribbon keyboard hints. Pressing Alt is a suspected trigger,
not yet confirmed by the user.

Environment: Ubuntu 24.04, new WoW64, desktop `:0`, Vulkan renderer,
144 DPI, `prefixes/inv`, Inventor's .NET 10.0.9 / CoreCLR 10.0.926.27113.
Native .NET Framework 4.8 is also installed, but is not the runtime named
in this crash.

## Evidence
Private local log: `inst/local/debug-20260928-190541/inventor.log`.
Wine exception `80131623`; the event log contains:

```text
System.Environment.FailFast
MS.Internal.Invariant.FailFast
MS.Internal.Shaping.TypefaceMap.MapUnresolvedCharacters
MS.Internal.Shaping.TypefaceMap.MapByFontFamilyList
MS.Internal.Shaping.TypefaceMap.GetShapeableText
...
System.Windows.Media.FormattedText.get_Width
Autodesk.Private.Windows.KeyTipAdorner.CreateAdornerContent
Autodesk.Private.Windows.KeyTipAdorner.CreateKeyTip
Autodesk.Private.Windows.KeyTipAdorner.OnRender
```

The prefix had Autodesk fonts, Arial Unicode MS and Liberation Sans, but no
standard Arial or Segoe UI files. Host fontconfig substitutes Liberation Sans
for Arial; that does not establish that WPF's font fallback can use it.
Missing fonts are a hypothesis, not a confirmed Wine root cause.
No Windows reference test has been run; this workstation has no reference VM.

## Installation / verification
User approved installing `corefonts` into this prefix only.
Use distro Winetricks **20240105**, whose individual font downloads have
SHA-256 checks in the recipe. No unpinned Winetricks update or host font install.
Corefonts supplies Arial and other Microsoft core fonts, **not Segoe UI**.

With Inventor closed, from the repository root:

```sh
export WINEPREFIX="$PWD/prefixes/inv"
export WINE="$PWD/build/wine"
export WINESERVER="$PWD/build/server/wineserver"
export DISPLAY=:0 WINEDEBUG=-all
"$WINESERVER" -k
"$WINESERVER" -w
disabled=AdskLicensingService.exe,AdskAccessServiceHost.exe,cer_service.exe
disabled="$disabled,FNPLicensingService64.exe,AdskAccessCore.exe,AdskAccessService.exe"
WINEDLLOVERRIDES="$disabled=d" winetricks --force -q corefonts
"$WINESERVER" -k
"$WINESERVER" -w
```

Do not stop this prefix if other Inventor sessions share its licensing service.
The initial attempt (`inst/local/corefonts-20260928-191435.log`) stalled:
Winetricks waits for `wineserver -w` after copying each font, but Autodesk's
persistent services prevent exit. The command above disables those executables
only for the installer's environment; service registry settings are untouched.
`--force` retries the partially copied Andale font rather than skipping it.
Do not carry these DLL overrides into the Inventor launch.
Retry log: `inst/local/corefonts-20260928-191838.log`.

Installation completed successfully. Verified `corefonts.installed`, the
regular/bold/italic/bold-italic Arial files, and
`HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts`:
`Arial (TrueType) = arial.ttf`.
The prefix was stopped afterwards to drop cached font data, then Inventor
relaunched without the temporary overrides.
Post-install log: `inst/local/debug-20260928-192035-corefonts/inventor.log`.

The post-install run exited 0, with no logged fatal exception or new Inventor
dump. User confirmation of the key-tip action is still pending.
A successful launch/exit alone does not verify the key-tip crash is fixed.
Segoe UI remains absent; do not install further fonts or declare a Wine fix
without evidence. The later captured CoreCLR access violation
([124](124-open-dialog-resize-coreclr-crash.md)) has a different signature;
the CJK Format Text preview problem ([125](125-format-text-preview-cjk-richedit.md))
is also not established as related.

## Next
When licensing permits, check ribbon key tips on the current build and record
the exact trigger if this FailFast recurs. Use the server's Windows reference
to distinguish a font prerequisite from Wine's WPF/font-fallback behavior.
