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
#   tools/regress.sh unit DLL:TEST [-b BUILD] [-a ARCHS] [-n REPEAT] [-j JOBS] [-t SECS] [-o DIR]
#     One test unit, REPEAT (1) times per arch (default both; BUILD defaults to build/), each in
#     a fresh prefix copy, same environment as `run`. Prints per-run results and pass/fail
#     counts; logs in DIR (default a /tmp/regress-unit.* dir). Takes no run lock: quick checks
#     don't queue behind full runs. Uses displays :152-:199 (full runs: :120-:151) and the
#     shared port lock, so it can't collide with a full run. Costs ~40 s for the template.
#   tools/regress.sh compare BASE.txt NEW.txt [-r REPS] [-j JOBS]
#     Lists units worse in NEW (higher status rank pass<fail<crash=timeout,
#     or more failures, or a new unit not passing), re-runs them REPS (2)
#     times on NEW's build and BASE's build (from the DIR/info files) and
#     classifies REAL (every NEW re-run worse, BASE re-runs fine) or FLAKY.
#     Units absent from BASE aren't re-run on BASE's build; they're listed as
#     NEW with their re-runs and "consistent" (no re-run passed) or "intermittent".
#     REAL lines get the commits BASE..NEW touching the module as suspects.
#
# Runs (and compare's re-runs) are serialized by flock on /tmp/regress.lock, so
# concurrent invocations, e.g. workers' subset runs, queue up instead of clashing.
# Units binding fixed localhost ports (webservices:proxy|channel, winhttp:notification|winhttp,
# wininet:http, httpapi:httpapi, rpcrt4:server) additionally hold /tmp/regress-ports.lock while running
# (shards of both arches run in parallel in one network namespace).
#
# Each of JOBS (32) shards gets its own prefix (cp -a of a template made with
# this build; ~1.8 GB each, in /dev/shm), Xvfb display (first free from :120)
# and HOME. Per unit: WINEDEBUG=-all, `module=b` like tools/runtest, a fresh
# cwd, timeout SECS (120; then the shard's wineserver is killed).
# Xvfb is 1920x1200x24 set to 1024x768, with extra RandR modes (tools/xvfb-modes.c) that the
# display-mode tests (user32:monitor, dxgi, ddraw, ...) need.
# Software rendering only (llvmpipe/lavapipe). Wine Gecko (deps/*.msi, sha256 checked against
# appwiz.cpl's GECKO_SHA) is installed in the template, no Mono; appwiz.cpl disabled so the
# install dialog can't hang tests; other network-dependent tests just fail consistently.
set -eu
shopt -s nullglob
root=$(cd "$(dirname "$0")/.." && pwd)
unset DRI_PRIME WINE_D3D_CONFIG WINEDLLOVERRIDES WINEARCH WINESERVER WINELOADER WINEPREFIX

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 1; }

# Kills (by PID) processes left over from prefixes under $1, e.g. services that outlived
# their wineserver, and says so.
sweep() {
    local p n=0
    for p in /proc/[0-9]*; do
        grep -qz "^WINEPREFIX=$1/" $p/environ 2> /dev/null && kill -9 ${p#/proc/} && n=$((n + 1))
    done
    [ $n = 0 ] || echo "regress: killed $n leftover processes of $1" >&2
}

# Runs "arch module unit [tag]" task lines from stdin on build $1, into dir $2.
run_units() {
    local build=$1 out=$2 jobs=$3 tmo=$4 W d s i a hp dl disps=() src gv gsha
    W=$(mktemp -d /dev/shm/regress.XXXXXX)
    trap "set +e; for s in \$(seq 0 $((jobs - 1))); do WINEPREFIX=$W/p\$s $build/server/wineserver -k 2>/dev/null; done; kill \$(jobs -p) 2>/dev/null; wait; sweep $W; rm -rf $W" EXIT
    trap exit INT TERM
    cat > "$W/tasks"
    exec {dl}> /tmp/regress-display.lock  # two invocations must not pick the same display
    flock $dl
    for ((d = ${dfirst:-120}; ${#disps[@]} < jobs; d++)); do
        [ $d -le ${dlast:-151} ] || { echo "regress: out of displays :${dfirst:-120}-:${dlast:-151}" >&2; exit 1; }
        [ -e /tmp/.X$d-lock ] || [ -e /tmp/.X11-unix/X$d ] && continue
        Xvfb :$d -screen 0 1920x1200x24 -nolisten tcp {dl}>&- > /dev/null 2>&1 &
        disps+=($d)
    done
    for d in "${disps[@]}"; do
        # the socket appears before Xvfb accepts clients (xvfb-modes then failed to connect)
        for ((i = 0; i < 50; i++)); do xdpyinfo -display :$d > /dev/null 2>&1 && break; sleep 0.2; done
        xdpyinfo -display :$d > /dev/null 2>&1 || { echo "regress: Xvfb :$d failed to start" >&2; exit 1; }
    done
    # Xvfb has one RandR mode; the holder adds more and must stay connected (it dies with Xvfb).
    gcc -O1 -o "$W/xvfb-modes" "$root/tools/xvfb-modes.c" -lX11 -lXrandr
    for d in "${disps[@]}"; do
        for ((a = 0; a < 3; a++)); do  # the holder prints "ready" once the modes are verified
            : > "$W/ready.$d"
            DISPLAY=:$d "$W/xvfb-modes" 640x480 800x600 1024x768 1280x720 1280x1024 1920x1080 {dl}>&- > "$W/ready.$d" 2>> "$W/modes.log" &
            hp=$!
            for ((i = 0; i < 150; i++)); do grep -q ready "$W/ready.$d" && break; kill -0 $hp 2> /dev/null || break; sleep 0.2; done
            grep -q ready "$W/ready.$d" && DISPLAY=:$d xrandr -s 1024x768 && continue 2
            kill $hp 2> /dev/null; echo "regress: modes not ready on :$d (attempt $((a + 1))/3)" >&2
        done
        cat "$W/modes.log" >&2; exit 1
    done
    exec {dl}>&-
    mkdir -p "$W/home" "$out"
    HOME=$W/home WINEPREFIX=$W/tpl DISPLAY=:${disps[0]} WINEDEBUG=-all \
        WINEDLLOVERRIDES="mscoree,mshtml=" "$build/wine" wineboot -u > "$W/tpl.log" 2>&1
    WINEPREFIX=$W/tpl timeout 120 "$build/server/wineserver" -w || true
    # Gecko: GECKO_SHA lines in appwiz.cpl are x86, x86_64, then "???"
    src=$build/$(sed -n 's/^srcdir = //p' "$build/Makefile")/dlls/appwiz.cpl/addons.c
    gv=$(sed -n 's/^#define GECKO_VERSION "\(.*\)"/\1/p' "$src")
    mapfile -t gsha < <(sed -n 's/^#define GECKO_SHA "\(.*\)"/\1/p' "$src")
    for a in x86:0 x86_64:1; do
        msi=$root/deps/wine-gecko-$gv-${a%:*}.msi
        echo "${gsha[${a#*:}]}  $msi" | sha256sum -c --quiet - \
            || { echo "regress: need $msi with the sha256 from appwiz.cpl (https://dl.winehq.org/wine/wine-gecko/$gv/)" >&2; exit 1; }
        HOME=$W/home WINEPREFIX=$W/tpl DISPLAY=:${disps[0]} WINEDLLOVERRIDES="mscoree,mshtml=" \
            "$build/wine" msiexec /i "$msi" /qn > "$W/gecko-${a%:*}.log" 2>&1
        WINEPREFIX=$W/tpl timeout 120 "$build/server/wineserver" -w || true
    done
    # wineboot ran with mshtml disabled, so its classes and MIME handlers (text/html) aren't registered
    for a in regsvr32 'C:\windows\syswow64\regsvr32.exe'; do
        HOME=$W/home WINEPREFIX=$W/tpl DISPLAY=:${disps[0]} WINEDLLOVERRIDES="mscoree=" \
            "$build/wine" "$a" /s mshtml.dll >> "$W/gecko-x86.log" 2>&1
        WINEPREFIX=$W/tpl timeout 120 "$build/server/wineserver" -w || true
    done
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
    local arch=$1 mod=$2 unit=$3 tag=${4:-} s fd pf exe log rc t0 err i d=($DISPS)
    for ((s = 0; ; s = (s + 1) % ${#d[@]})); do
        exec {fd}> "$W/slot$s.lock"
        flock -n $fd && break
        exec {fd}>&-
        [ $s = $((${#d[@]} - 1)) ] && sleep 1
    done
    # Fixed-port servers: one at a time, across shards, arches and runs. Held until the awk below.
    exec {pf}> /tmp/regress-ports.lock
    case $mod:$unit in webservices:proxy|webservices:channel|winhttp:notification|winhttp:winhttp|wininet:http|httpapi:httpapi|rpcrt4:server)
        flock $pf;;
    esac
    exe=$build/dlls/$mod/tests/$arch-windows/${mod}_test.exe
    [ -e "$exe" ] || exe=$build/programs/${mod%.exe}/tests/$arch-windows/${mod}_test.exe
    log=$out/logs/$arch/$mod/$unit${tag:+.$tag}.log
    mkdir -p "${log%/*}"
    rm -rf "$W/c$s" && mkdir "$W/c$s" && cd "$W/c$s"
    t0=$EPOCHREALTIME rc=0
    { HOME=$W/h$s WINEPREFIX=$W/p$s DISPLAY=:${d[$s]} WINEDLLOVERRIDES="$mod=b;appwiz.cpl=d" \
        timeout -k 10 "$tmo" "$build/wine" "$exe" "$unit" > "$log" 2>&1 < /dev/null {fd}>&- {pf}>&-; } 2> /dev/null || rc=$?
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
    exec {pf}>&-
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

cmd_unit() {
    local unit=${1:?DLL:TEST} build=$root/build out= jobs=4 archs="x86_64 i386" n=1 tmo=120 opt arch i
    shift
    while getopts b:o:j:a:n:t: opt; do
        case $opt in b) build=$(cd "$OPTARG" && pwd);; o) out=$OPTARG;; j) jobs=$OPTARG;; a) archs=$OPTARG;;
                     n) n=$OPTARG;; t) tmo=$OPTARG;; *) usage;; esac
    done
    [[ $unit == *:* ]] || usage
    out=$(realpath -m "${out:-$(mktemp -d /tmp/regress-unit.XXXXXX)}")
    rm -f "$out/results.part"; mkdir -p "$out"
    for arch in $archs; do
        for ((i = 1; i <= n; i++)); do echo "$arch ${unit%:*} ${unit#*:} r$i"; done
    done > "$out/tasks"
    [ $jobs -gt $(wc -l < "$out/tasks") ] && jobs=$(wc -l < "$out/tasks")
    (dfirst=152 dlast=199 run_units "$build" "$out" "$jobs" "$tmo" < "$out/tasks")
    sort "$out/results.part" | tee "$out/results.txt"
    awk '{ c[$1" "$3]++; if ($3 == "fail") f[$1] = f[$1] " " $4 }
        END { for (k in c) print k, c[k]; for (a in f) print a, "failure counts:" f[a] }' "$out/results.txt" | sort
    echo "logs: $out/logs/"
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

# Full runs and compare hold /tmp/regress.lock for their whole duration. flock -o closes the
# fd for the command, so no Wine process can keep the lock after we're gone.
if [ "${1:-}" != unit ] && [ -z "${RG_LOCKED:-}" ]; then
    export RG_LOCKED=1
    flock -n /tmp/regress.lock true || echo "regress: waiting for another run (/tmp/regress.lock)" >&2
    exec flock -o /tmp/regress.lock "$0" "$@"
fi

case ${1:-} in
    run) shift; cmd_run "$@";;
    unit) shift; cmd_unit "$@";;
    compare) shift; cmd_compare "$@";;
    *) usage;;
esac
