# 004 CreateProcess succeeds when an exe manifest dependency is missing
Status: open · Owner: - · Branch: - · Found in: tests/sxs_probe (by 002 worker)

## Symptom
Exe with an embedded manifest that depends on an assembly that can't be found:
Windows fails CreateProcess with ERROR_SXS_CANT_GEN_ACTCTX (14001); Wine starts
the process with an empty default activation context.
Ground truth: tests/sxs_probe/windows.txt cases none, sub_nocfg, wrongname,
nons_cfg, dotnet_cfg (launch.c prints the CreateProcess error).

## Task
Low priority (only matters for apps that probe for this). Confirm where Windows
fails (parent CreateProcess vs child init), match it, kernel32 test.
