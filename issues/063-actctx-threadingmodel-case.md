# 063 Stress Analysis hangs Inventor: manifest threadingModel="free" parsed case-sensitively
Status: open (draft) · Owner: - · Branch: - · Found in: specialised-environments pass (inv3/:100, integ c036c687c47)

## Symptom
Part open, 3D Model → Stress Analysis (or Environments → Stress Analysis): Inventor's UI
freezes for good (no repaint, no input), 0% CPU. Every time, first entry per session.

## Evidence (winedbg bt, Inventor main thread 0x2c0)
```
WaitForSingleObject                     <- INFINITE
fea_addin_utils  ROTUtils::RegisterCommunicator (+0x7e2d2)
fea_application_common (+0x2ab9e ...)   <- add-in activation from the ribbon command
```
`RegisterCommunicator` (Autodesk code, Ghidra) creates event "CommunicatorNamedEvent", starts
an MFC thread and waits for it. That thread: `CoInitializeEx(NULL, COINIT_MULTITHREADED)`,
activates FEA_Application_Common.dll's manifest context, `CoCreateInstance({E4E2FE54-5A90-
4006-AC58-9E11DE19817B}, CLSCTX 0x17)`, registers it in the ROT, then sets the event. On Wine it
is stuck in:
```
apartment_hostobject_in_hostapt(multi_threaded=0, main_apartment=1)   combase/apartment.c
apartment_get_inproc_class_object(class_context=0x17)
CoCreateInstance(rclsid={E4E2FE54-...})
fea_addin_utils (+0x7e3dd)
```
i.e. combase treats the class as main-threaded and marshals the creation to the main STA,
which is the blocked UI thread: deadlock.

The class comes from FEA_Application_Common.dll's embedded manifest:
`<comClass clsid="{E4E2FE54-5A90-4006-AC58-9E11DE19817B}" ... threadingModel="free">`
(lowercase). ntdll `parse_com_class_threadingmodel` compares with `xmlstr_cmp`
(case-sensitive), so "free" becomes ThreadingModel_No (= main-threaded).

## Windows ground truth
`tests/actctx_tmodel.c` (manifest with one comClass, FindActCtxSectionGuid COM server
redirection, model field): 1 Apartment, 2 Free, 3 No, 4 Both, 5 Neutral.

| threadingModel | Win11 VM | Wine |
|---|---|---|
| Apartment / apartment | 1 / 1 | 1 / 3 |
| Free / free / FREE | 2 / 2 / 2 | 2 / 3 / 3 |
| Both / both | 4 / 4 | 4 / 3 |
| Neutral / neutral | 5 / 5 | 5 / 3 |
| "" / "bogus" | CreateActCtx fails 14001 | 3 / 3 |

## Task
Case-insensitive threadingModel values in ntdll actctx (and, per the table, reject empty /
unknown values). Add the cases to kernel32/tests/actctx.c. Then retest: Stress Analysis
environment → Create Simulation (specialised-environments pass, beam.cs scenario part).
