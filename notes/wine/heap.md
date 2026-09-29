# ntdll heap — checked at wine-11.18-218-g4e819f054dd (+ fix/073)

- `dlls/ntdll/heap.c`: backend = subheaps with free lists by size class
  (`get_free_list_index`, log-linear, `FREE_LIST_LINEAR_BITS` per power of two), blocks
  ≥ `HEAP_MIN_LARGE_BLOCK_SIZE` (~1 MiB x64) are separate VirtualAllocs. Free blocks within a
  class are unsorted (LIFO; the subheap's last block goes to the tail).
- LFH frontend exists (`heap_allocate_block_lfh`, bins ≤ 32 KiB, groups of 31 blocks allocated
  from the backend) and only for HEAP_GROWABLE heaps; `bin_try_enable` turns bins on by
  alloc counts (small bins quickly, bins ≥1 KiB only with > 4 MB live in the bin).
  HeapCompatibilityInformation reads 0 until the first bin enables; Win11 reports 2 from start.
- Header size is pinned by kernel32:heap's fixed-size heap test (`HeapCreate(0, N, N)` must fit
  an alloc of N-0xc00 but not N-0x600 on x64), so `struct heap` can't grow by more than ~700 B.
- `find_free_block` pre-073 walked every too-small block of the class: O(free blocks) per alloc on
  fragmented heaps (Inventor after hours: 9% CPU). Repro: `tests/heap_stress.c` (issue 073).
- Profiling: perf on a Wine PE module shows `ntdll.dll 0x5xxxx` offsets; `objdump -d -l
  --start-address=0x170000000+OFF build/dlls/ntdll/x86_64-windows/ntdll.dll` maps them to source
  (image base 0x170000000, must match the build that ran).
