# 073 ntdll heap: find_free_block linear free-list walk dominates after hours (fragmentation)
Status: open (draft) · Owner: - · Branch: - · Found in: soak test (inst/soak/2026-09-28, integ c036c687c47)

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
Not measured. Windows' NT heap indexes large free blocks (ListHints bitmap / sorted big-block list)
and apps opt into the segment heap; allocation cost doesn't grow with free-list length like this.

## Next steps
- Repro: a small program that fragments a heap with mixed large blocks (e.g. 100k live blocks of
  random 1-64 KiB sizes, free every other), then times 100k large allocations; Wine vs VM.
- Fix direction: a size-ordered structure (tree or more classes) for large free blocks.
