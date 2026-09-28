#!/bin/bash
# regress.sh — differential runner for Wine's conformance suite.
#
#   tools/regress.sh run BUILD [-o DIR] [-j JOBS] [-a ARCHS] [-m REGEX] [-t SECS] [-f]
#     Runs every `<module>_test.exe <unit>` of the out-of-tree build BUILD
#     (configured --enable-archs=i386,x86_64). ARCHS: "x86_64 i386" (default)
#     or one of them. REGEX filters module names (`-m '^(ntdll|kernel32)$'`);
#     workers use that on their touched modules, full runs are the coordinator's.
#     DIR defaults to deps/regress/<commit BUILD was built from, per `wine
#     --version`> (baseline cache: an existing DIR/results.txt is reused
#     unless -f; uncommitted changes aren't in the key). Writes:
#       DIR/results.txt  "arch module:unit status failures todo skipped time"
#                        status: pass|fail|crash|timeout, time in s
#       DIR/logs/ARCH/MODULE/UNIT.log, DIR/info (build, commit, wall time)
#   tools/regress.sh compare BASE.txt NEW.txt [-r REPS] [-j JOBS]
#     Lists units worse in NEW (higher status rank pass<fail<crash=timeout,
#     or more failures, or a new unit not passing), re-runs them REPS (2)
#     times on NEW's build and BASE's build (from the DIR/info files) and
#     classifies REAL (every NEW re-run worse, BASE re-runs fine) or FLAKY.
#     Units absent from BASE aren't re-run on BASE's build; they're listed as
#     NEW with their re-runs and "consistent" (no re-run passed) or "intermittent".
#     REAL lines get the commits BASE..NEW touching the module as suspects.
#
# Each of JOBS (32) shards gets its own prefix (cp -a of a template made with
# this build; ~1.8 GB each, in /dev/shm), Xvfb display (first free from :120)
# and HOME. Per unit: WINEDEBUG=-all, `module=b` like tools/runtest, a fresh
# cwd, timeout SECS (120; then the shard's wineserver is killed).
# Software rendering only (llvmpipe/lavapipe). No Gecko/Mono installed and
# appwiz.cpl disabled so the install dialog can't hang tests; network-dependent
# tests just fail consistently.
set -eu
shopt -s nullglob
root=$(cd "$(dirname "$0")/.." && pwd)
unset DRI_PRIME WINE_D3D_CONFIG WINEDLLOVERRIDES WINEARCH WINESERVER WINELOADER WINEPREFIX

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 1; }

# Runs "arch module unit [tag]" task lines from stdin on build $1, into dir $2.
run_units() {
    local build=$1 out=$2 jobs=$3 tmo=$4 W d s i disps=()
    W=$(mktemp -d /dev/shm/regress.XXXXXX)
    trap "set +e; for s in \$(seq 0 $((jobs - 1))); do WINEPREFIX=$W/p\$s $build/server/wineserver -k 2>/dev/null; done; kill \$(jobs -p) 2>/dev/null; wait; rm -rf $W" EXIT
    trap exit INT TERM
    cat > "$W/tasks"
    for ((d = 120; ${#disps[@]} < jobs; d++)); do
        [ -e /tmp/.X$d-lock ] || [ -e /tmp/.X11-unix/X$d ] && continue
        Xvfb :$d -screen 0 1024x768x24 -nolisten tcp > /dev/null 2>&1 &
        disps+=($d)
    done
    for d in "${disps[@]}"; do
        for ((i = 0; i < 50; i++)); do [ -e /tmp/.X11-unix/X$d ] && break; sleep 0.2; done
        [ -e /tmp/.X11-unix/X$d ] || { echo "regress: Xvfb :$d failed to start" >&2; exit 1; }
    done
    mkdir -p "$W/home" "$out"
    HOME=$W/home WINEPREFIX=$W/tpl DISPLAY=:${disps[0]} WINEDEBUG=-all \
        WINEDLLOVERRIDES="mscoree,mshtml=" "$build/wine" wineboot -u > "$W/tpl.log" 2>&1
    WINEPREFIX=$W/tpl timeout 120 "$build/server/wineserver" -w || true
    WINEPREFIX=$W/tpl "$build/server/wineserver" -k 2>/dev/null || true  # never leave template processes behind
    for s in $(seq 0 $((jobs - 1))); do
        mkdir "$W/h$s"; cp -a "$W/tpl" "$W/p$s"
    done
    export W build out tmo DISPS="${disps[*]}"
    export WINEDEBUG=-all WINETEST_PLATFORM=wine WINETEST_DEBUG=1 LIBGL_ALWAYS_SOFTWARE=1 \
        VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
        __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
    export -f run_one
    xargs -P "$jobs" -L 1 bash -c 'run_one "$@"' _ < "$W/tasks" >> "$out/results.part"
}

# One unit on a free shard slot; prints its results line.
run_one() {
    local arch=$1 mod=$2 unit=$3 tag=${4:-} s fd exe log rc t0 err i d=($DISPS)
    for ((s = 0; ; s = (s + 1) % ${#d[@]})); do
        exec {fd}> "$W/slot$s.lock"
        flock -n $fd && break
        exec {fd}>&-
        [ $s = $((${#d[@]} - 1)) ] && sleep 1
    done
    exe=$build/dlls/$mod/tests/$arch-windows/${mod}_test.exe
    [ -e "$exe" ] || exe=$build/programs/${mod%.exe}/tests/$arch-windows/${mod}_test.exe
    log=$out/logs/$arch/$mod/$unit${tag:+.$tag}.log
    mkdir -p "${log%/*}"
    rm -rf "$W/c$s" && mkdir "$W/c$s" && cd "$W/c$s"
    t0=$EPOCHREALTIME rc=0
    { HOME=$W/h$s WINEPREFIX=$W/p$s DISPLAY=:${d[$s]} WINEDLLOVERRIDES="$mod=b;appwiz.cpl=d" \
        timeout -k 10 "$tmo" "$build/wine" "$exe" "$unit" > "$log" 2>&1 < /dev/null {fd}>&-; } 2> /dev/null || rc=$?
    [ $rc = 124 ] || [ $rc = 137 ] && WINEPREFIX=$W/p$s "$build/server/wineserver" -k 2>/dev/null
    # A dead display (e.g. someone's pkill Xvfb) makes every GUI test fail: abort the run.
    # Retried: connects were seen to fail transiently right after uxtheme:system
    # under full load, with Xvfb still alive.
    for ((i = 0; ; i++)); do
        err=$(xdpyinfo -display :${d[$s]} 2>&1 > /dev/null) && break
        if [ $i = 5 ]; then
            echo "regress: display :${d[$s]} died during $arch $mod:$unit, aborting: $err" >&2
            pgrep -a -f "^Xvfb :${d[$s]} " >&2
            exit 255
        fi
        sleep 2
    done
    awk -v k="$arch $mod:$unit" -v rc=$rc -v t0=$t0 -v t1=$EPOCHREALTIME '
        match($0, /[0-9]+ tests executed \([0-9]+ marked as todo, [0-9]+ as flaky, [0-9]+ failures?\), [0-9]+ skipped\./) {
            s = substr($0, RSTART, RLENGTH); gsub(/[^0-9]+/, " ", s); split(s, v, " ")
            n++; todo += v[2]; fail += v[4]; skip += v[5]
        }
        / unhandled exception [0-9a-f]+ at / { crash = 1 }
        END {
            st = rc == 124 || rc == 137 ? "timeout" : !n || crash || (rc && !fail) ? "crash" : fail || rc ? "fail" : "pass"
            printf "%s %s %d %d %d %.1f\n", k, st, fail, todo, skip, t1 - t0
        }' "$log"
}

# Prints "arch module unit" for every test unit of build $1 (module regex $2).
list_units() {
    local arch f mod
    for arch in $3; do
        for f in "$1"/{dlls,programs}/*/tests/$arch-windows/*_test.exe; do
            mod=${f##*/} mod=${mod%_test.exe}
            [[ $mod =~ $2 ]] || continue
            sed -n 's/^extern void func_\(.*\)(void);$/\1/p' "${f%/*/*}/testlist.c" | sed "s/^/$arch $mod /"
        done
    done
}

cmd_run() {
    local build out= jobs=32 archs="x86_64 i386" re=. tmo=120 force= src commit t0 opt
    build=$(cd "${1:?}" && pwd); shift
    while getopts o:j:a:m:t:f opt; do
        case $opt in o) out=$OPTARG;; j) jobs=$OPTARG;; a) archs=$OPTARG;; m) re=$OPTARG;;
                     t) tmo=$OPTARG;; f) force=1;; *) usage;; esac
    done
    src=$(sed -n 's/^srcdir = //p' "$build/Makefile")
    commit=$("$build/wine" --version)  # wine-X.Y-N-gHASH, from git describe at build time
    commit=$(git -C "$build/$src" rev-parse "${commit##*-g}")
    out=$(realpath -m "${out:-$root/deps/regress/$commit}")
    if [ -e "$out/results.txt" ] && [ -z "$force" ]; then echo "cached: $out/results.txt"; return; fi
    rm -rf "$out/logs" "$out/results.txt" "$out/results.part"; mkdir -p "$out"
    t0=$(date +%s)
    list_units "$build" "$re" "$archs" | run_units "$build" "$out" "$jobs" "$tmo"
    sort "$out/results.part" > "$out/results.txt"; rm "$out/results.part"
    printf 'build=%s\nsrcdir=%s\ncommit=%s\narchs="%s"\njobs=%s\nwall=%s\n' "$build" \
        "$(cd "$build/$src" && pwd)" "$commit" "$archs" "$jobs" $(($(date +%s) - t0)) > "$out/info"
    echo "$out/results.txt: $(wc -l < "$out/results.txt") units in $(($(date +%s) - t0)) s;" \
        $(awk '{c[$3]++} END {for (s in c) printf "%s=%d ", s, c[s]}' "$out/results.txt")
}

cmd_compare() {
    local base=${1:?} new=${2:?} reps=2 jobs=32 opt W n bdir= ndir nsrc bc= nc= line m
    shift 2
    while getopts r:j: opt; do
        case $opt in r) reps=$OPTARG;; j) jobs=$OPTARG;; *) usage;; esac
    done
    W=$(mktemp -d)
    # worse units: "key base_status base_fail new_status new_fail"
    awk 'function r(s) { return s == "pass" ? 0 : s == "fail" ? 1 : 2 }
        NR == FNR { bs[$1" "$2] = $3; bf[$1" "$2] = $4; next }
        { k = $1" "$2
          if (!(k in bs) ? $3 != "pass" : r($3) > r(bs[k]) || ($3 == "fail" && bs[k] == "fail" && $4 > bf[k]))
              print k, (k in bs ? bs[k] : "-"), (k in bf ? bf[k] : 0), $3, $4 }' "$base" "$new" > "$W/worse"
    echo "$(wc -l < "$W/worse") worse of $(wc -l < "$new") units ($(awk 'NR==FNR {k[$1" "$2]; next} !($1" "$2 in k)' "$new" "$base" | wc -l) missing in NEW)"
    [ -s "$W/worse" ] || return 0
    for ((n = 1; n <= reps; n++)); do
        awk -v n=$n '{ split($2, m, ":"); print $1, m[1], m[2], "r" n }' "$W/worse"
    done > "$W/tasks"
    ndir=$(sed -n 's/^build=//p' "$(dirname "$new")/info")
    nsrc=$(sed -n 's/^srcdir=//p' "$(dirname "$new")/info"); nc=$(sed -n 's/^commit=//p' "$(dirname "$new")/info")
    if [ -e "$(dirname "$base")/info" ]; then
        bdir=$(sed -n 's/^build=//p' "$(dirname "$base")/info"); bc=$(sed -n 's/^commit=//p' "$(dirname "$base")/info")
    fi
    [ $jobs -gt $(wc -l < "$W/tasks") ] && jobs=$(wc -l < "$W/tasks")
    (run_units "$ndir" "$W/new" "$jobs" 120 < "$W/tasks")
    awk 'NR == FNR { if ($3 == "-") new[$1" "$2]; next } !($1" "$2":"$3 in new)' "$W/worse" "$W/tasks" > "$W/btasks"
    [ -n "$bdir" ] && [ -x "$bdir/wine" ] && [ -s "$W/btasks" ] && (run_units "$bdir" "$W/base" "$jobs" 120 < "$W/btasks")
    mkdir -p "$W/base"; touch "$W/base/results.part"
    awk 'function r(s) { return s == "pass" ? 0 : s == "fail" ? 1 : 2 }
        function worse(s, f, k) { return r(s) > r(bs[k]) || (s == "fail" && bs[k] == "fail" && f > bf[k]) }
        FNR == 1 { f++ }
        f == 1 { k = $1" "$2; bs[k] = $3; bf[k] = $4; ns[k] = $5; nf[k] = $6; order[++n] = k; next }
        f == 2 { k = $1" "$2; nr[k] = nr[k] " " $3 "/" $4; if (!worse($3, $4, k)) ok[k] = 1; if ($3 == "pass") np[k] = 1; next }
        { k = $1" "$2; br[k] = br[k] " " $3 "/" $4; if (worse($3, $4, k)) ok[k] = 1 }
        END { for (i = 1; i <= n; i++) { k = order[i]
            if (bs[k] == "-") printf "NEW   %s %s/%d | new:%s (%s)\n", k, ns[k], nf[k], nr[k], k in np ? "intermittent" : "consistent"
            else printf "%s %s %s/%d -> %s/%d | new:%s | base:%s\n", k in ok ? "FLAKY" : "REAL ", k,
                bs[k], bf[k], ns[k], nf[k], nr[k], k in br ? br[k] : " -" } }' \
        "$W/worse" "$W/new/results.part" "$W/base/results.part" | sort -k1,1r -k2 | while read -r line; do
        echo "$line"
        set -- $line
        if [ "$1" = REAL ] && [ -n "$bc" ]; then
            m=${3%%:*}
            git -C "$nsrc" log --format='    suspect %h %s' "$bc..$nc" -- "dlls/$m" "programs/${m%.exe}" | head -3
        fi
    done
    rm -rf "$W"
}

case ${1:-} in
    run) shift; cmd_run "$@";;
    compare) shift; cmd_compare "$@";;
    *) usage;;
esac
