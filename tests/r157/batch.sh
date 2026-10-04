#!/bin/bash
# Host side: the 157 stress matrix in the current vmwl session. Usage: tests/r157/batch.sh COMPOSITOR BUILD RUNS [STRESS_RUNS]
#   rapid 300 and rapid 3000 RUNS times each, lockstress 3000 with seeds 1..STRESS_RUNS, the last two with WAYLAND_DEBUG=1.
#   Summary lines on stdout; logs and hang backtraces stay in the guest under ~/r157/COMPOSITOR-<build>-*.
cd "$(dirname "$0")/../.."
C=$1; B=$2; N=$3; S=${4:-$((N / 2))}; T=$C-$(basename $B)
g() { vmwl/wl.sh "$@" 2>&1 | grep -v ' Killed '; }
# JITTER=1: relative pointer motion and key presses in the guest during the whole batch (tests/r157/jitter.py vm)
[ -z "${JITTER:-}" ] || { python3 tests/r157/jitter.py vm 100000 640 400 >/dev/null 2>&1 & jp=$!; trap "kill $jp 2>/dev/null" EXIT; }
g /host/inst/157/g-run.sh $B $T-rapid300 $N 60 inst/134-review/rv.exe rapid 300
g /host/inst/157/g-run.sh $B $T-rapid3000 $N 300 inst/134-review/rv.exe rapid 3000
for s in $(seq $S); do
  dbg=; [ $s -gt $((S - 2)) ] && dbg=WAYLAND_DEBUG=1
  g $dbg /host/inst/157/g-run.sh $B $T-stress-$s 1 300 inst/157/lockstress.exe 3000 $s
done
