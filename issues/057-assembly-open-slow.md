# 057 Opening/reopening multi-file assemblies 2.5–7x slower than Windows
Status: open (draft, perf) · Owner: - · Branch: - · Found in: samples campaign (inv3, integ 38e4c1c00ce)

## Observed (Wine / VM seconds, 2022 sample set)
Buffer Prep Skid.iam (1313 occurrences, 453 files): open 62.2 / 20.9, reopen
74.9 / 10.7; Test Station.iam open 15.4 / 5.9, reopen 15.4 / 3.9; Personal
Computer.iam open 13.0 / 5.6. Saves ~1.2x, single parts ~VM speed. So the cost is
per referenced file (file resolution / open / project search / OLE storage).
Note the VM runs WARP; open is mostly CPU+I/O, not graphics.

## Task
Profile a reopen of Buffer Prep Skid (perf/+relay counts on file APIs:
NtCreateFile/NtQueryAttributesFile/FindFirstFile counts, path lookups through
case-insensitive dir scans (Wine's biggest file-open cost on Linux), StgOpenStorage,
registry), compare API call counts with a VM ProcMon-free estimate if needed.
Candidates: case-insensitive lookup cost (casefold ext4 dir or Wine's dir cache),
repeated project/library search paths, storage (ole32 structured storage) reads.
