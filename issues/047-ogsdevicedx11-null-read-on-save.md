# 047 Inventor crash: NULL read in ogsdevicedx11 on a TBB worker (asm save)
Status: open (draft) · Owner: - · Branch: - · Found in: invscen suite, integ 228616fa47c (prefixes/inv, :98)

## Symptom
`tools/invscen/run.sh all` (cold start): asmbig `save` (SaveAs of the 200-occurrence
big.iam) failed after 12.5 s with RPC_E 0x800706BE: Inventor died, CER wrote a dump,
the next scenario restarted it. Warm rerun of the suite: save PASS (1.1 s).
Same signature in two older dumps from the campaign runs (integ 91495f487ad,
10:46 and 11:16), so not new in this build; 3 of the 12 dumps in the prefix.

## Evidence
Dumps (prefixes/inv/drive_c/users/xl0/AppData/Local/Temp/):
Inventor260928143009.dmp (this run), Inventor260928104637.dmp, Inventor260928111601.dmp.
`winedbg 'C:\users\xl0\AppData\Local\Temp\<dmp>'` in the prefix, faulting thread:

    page fault on read access to 0x0000000000000000
    0 ogsdevicedx11+0x32ebb
    1 ogsgraphics+0x30bda9
    2 ogsgraphics+0xecb50
    3 ogsgraphics+0xeef33
    4-8 tbb12 (worker thread) <- ucrtbase _beginthreadex_trampoline

Graphics device (D3D11, wined3d-vk) work on a TBB thread; probably the save-time
thumbnail/preview render (cf. 037, SaveAsBitmap unshaded). Intermittent.

## Next
Disassemble ogsdevicedx11+0x32ebb (Autodesk code, allowed) to see which D3D11 call's
result is NULL (failed Map / CreateX / GetBuffer?); rerun asmbig save in a loop with
WINEDEBUG=+d3d11 warn to catch the failing call.
