# 073 ntdll heap: find_free_block linear free-list walk dominates after hours (fragmentation)
Status: fixed · Owner: 073 worker · Branch: fix/073-heap-free-list (0241cf5dcb2, b203a80e0aa) · Found in: soak test (inst/soak/2026-09-28, integ c036c687c47)

## Symptom
Part of the Inventor soak slowdown (see 072 for the numbers): one suite run in a 4.2 h old
session vs the same suite in a fresh session, perf of Inventor at 199 Hz:

| | late | fresh |
|---|---|---|
| ntdll `find_free_block` loop (heap.c:1119-1123, ntdll+5ae50) | 8.8% of all samples (~41 CPU-s) | 0.17% (~0.7 CPU-s) |
| ntdll.dll total | 17.4% | 2.7% |

Present in every 10% slice of the late run (5-23%), not a burst. Geometry-heavy steps with few
COM calls (mass properties, rebuild) slowed down as much as COM-heavy ones (+120-180% over
4 h), consistent with a general allocator cost.

## Cause (Wine source)
`dlls/ntdll/heap.c:find_free_block()` starts at the request's size class (61 log-linear classes,
4 per power of two) and walks the free lists forward block by block until one is large enough.
Blocks within a class are unsorted, so every free block of that class smaller than the request is
visited; the top class (everything above its bound) is one list. With a fragmented heap (many free
blocks in the request's class) each allocation scans thousands of entries. Which classes are long
in Inventor is not measured yet (a `+heap` dump or gdb walk of `free_lists` would tell). Inventor's RSS grows 3 → 14 GB over
the session, so the heap holds many large free blocks.

## Windows
`HeapQueryInformation(HeapCompatibilityInformation)` on Win11: 2 (LFH) for the process heap, the
msvcrt heap and a fresh `HeapCreate(0,0,0)` heap, already at process start. Wine: 0 until a bin
activates, 2 after (cosmetic difference; Wine's LFH frontend exists and auto-enables per bin for
growable heaps, up to 32 KiB blocks — not the problem). Windows keeps the fragmenting stress flat.

## Repro
`tests/heap_stress.c` (`heap_stress.exe OPS_M LIVE MAXSIZE KEEP_PERMILLE`): each op frees a random
slot of LIVE and allocates a log-uniform 16..MAXSIZE block, KEEP‰ are leaked (pins holes).
`60 20000 262144 2`, ns per alloc+free / commit MB, per million ops:

| M ops | master | fix | Win11 VM |
|---|---|---|---|
| 1 | 635 / 395 | 482 / 334 | 992 / 608 |
| 10 | 1044 / 1273 | 356 / 1040 | 898 / 1080 |
| 20 | 2210 / 2126 | 365 / 1756 | 946 / 1596 |
| 30 | 2987 / 3026 | 336 / 2494 | 1014 / 2122 |
| 60 | 6263 / 5820 | 347 / 4881 | - |

master: 65% of samples on the same `find_free_block` load as in Inventor; fix: ~11% (bounded).
Non-fragmenting runs (`20 20000 262144`, `20 50000 65536`, `20 200 131072 0`) got faster too
(458→305, 546→286, 236→218 ns). (Windows' higher ns/op is page-fault cost: it decommits free memory.)

## Fix
1. 8 free lists per power of two instead of 4 (finer classes: shorter walks, better fits, -16..18%
   commit). Marker entries replaced by plain `struct list` heads: 113 entry markers would grow the
   heap header past what kernel32:heap's fixed-size-heap test allows (both an upper and a lower
   bound on the header overhead: 0x600..0xc00 on x64); list heads keep it about the same.
2. `find_free_block` looks at ≤32 blocks per list, then takes the first block of the next non-empty
   larger list (always fits), walking the whole class only when nothing larger is free.
Variants measured (bits/limit, 30 M ops of the stress): 2/∞ 3317 ns 3023 MB; 2/1 394 ns 4284 MB
(round-up alone fragments badly); 2/32 356 ns 3251 MB; 3/32 342 ns 2492 MB; 4/32 325 ns 2118 MB
(doesn't fit the header). Tests: kernel32:heap, ntdll:rtl clean both arches; regress on ntdll kernel32
kernelbase msvcrt ucrtbase msvcp140 oleaut32: 0 worse; full suite: 0 REAL (4 FLAKY).
Not verified on Inventor (needs a multi-hour session).
