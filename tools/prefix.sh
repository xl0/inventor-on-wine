#!/usr/bin/env bash
# Prefix/display/GPU/build setup from x/prefixes.tsv; leases in x/leases (git-ignored).
# Usage: tools/prefix.sh status [NAME]            wineserver, build, procs, licensing port, lease
#        tools/prefix.sh env NAME [--defaults]    export lines for eval (--defaults: only unset vars)
#        tools/prefix.sh start|stop NAME          [--holder H] [--force (stop the licensing host)]
#        tools/prefix.sh lease NAME [HOLDER] | release NAME [HOLDER]     [--force]
# HOLDER defaults to $PREFIX_HOLDER. start/stop refuse on a prefix leased to another holder.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD T=x/prefixes.tsv L=x/leases PORT=39683
HOLDER=${PREFIX_HOLDER:-} FORCE= DEFAULTS= A=()
cmd=${1:?usage: $0 status|env|start|stop|lease|release [NAME]}; shift
while [ $# -gt 0 ]; do case $1 in
	--force) FORCE=1;; --defaults) DEFAULTS=1;; --holder) HOLDER=$2; shift;; *) A+=("$1");; esac; shift; done
die() { echo "prefix.sh: $*" >&2; exit 1; }

load() { # NAME -> N DISP BUS DRI VNC BUILD ROLE, WP
	N=$1; local row; row=$(awk -F'\t' -v n="$N" '$1 == n' $T)
	[ -n "$row" ] || die "no prefix '$N' in $T"
	IFS=$'\t' read -r _ DISP BUS DRI VNC BUILD ROLE <<<"$row"
	WP=$ROOT/prefixes/$N; [ "${BUILD:0:1}" = / ] || BUILD=$ROOT/$BUILD
}
lease_holder() { awk -v n="$1" '$1 == n { print $2 }' $L 2>/dev/null || true; }
lease_line() { awk -v n="$1" '$1 == n { print $2, $3 }' $L 2>/dev/null || true; }
check_lease() { local h; h=$(lease_holder "$N"); [ -z "$h" ] || [ "$h" = "$HOLDER" ] || die "$N is leased to $h (use --holder $h)"; }

# wineserver pid of the loaded prefix: its cwd is the server dir /tmp/.wine-UID/server-<dev>-<inode of prefix dir>
server_pid() {
	local d p; d=$(printf '/tmp/.wine-%d/server-%s-%x' $UID "$(stat -c %D "$WP")" "$(stat -c %i "$WP")")
	for p in $(pgrep -x wineserver || true); do
		if [ "$(readlink /proc/$p/cwd 2>/dev/null)" = "$d" ]; then echo $p; return; fi
	done
}
# processes with this WINEPREFIX in their environ, minus our ancestors and shells
prefix_pids() {
	local anc=" " p=$$ f
	while [ "$p" -gt 1 ]; do anc+="$p "; p=$(awk '/^PPid/ { print $2 }' /proc/$p/status); done
	for f in $(grep -lxsz "WINEPREFIX=$WP" /proc/[0-9]*/environ || true); do
		p=${f#/proc/}; p=${p%/environ}
		case $anc in *" $p "*) continue;; esac
		case $(cat /proc/$p/comm 2>/dev/null) in bash|sh|zsh|dash|timeout|setsid|make|tee|sleep|"") continue;; esac
		echo $p
	done
}
port_pid() { ss -ltnpH "sport = :$PORT" | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2 || true; }

emit() { if [ -n "$DEFAULTS" ]; then printf ': "${%s:=%q}"; export %s\n' $1 "$2" $1; else printf 'export %s=%q\n' $1 "$2"; fi; }

status() {
	load $1
	local sp exe b pp procs comm p l
	sp=$(server_pid); pp=$(port_pid); l=$(lease_line $N)
	if [ -n "$sp" ]; then
		exe=$(readlink /proc/$sp/exe 2>/dev/null) || exe=?
		b=${exe%/server/wineserver}; b=${b#$ROOT/}
		procs=; for p in $(prefix_pids); do
			comm=$(cat /proc/$p/comm 2>/dev/null) || continue
			case $comm in Inventor.exe|AdskLicensing*) procs+=" $comm:$p";; esac
		done
		printf '%-8s %-5s up   wineserver %s build=%s  n=%d  port=%s  lease=%s\n' $N $DISP $sp "$b" "$(prefix_pids | wc -l)" \
			"$([ "$pp" = "$sp" ] && echo HELD || echo -)" "${l:--}"
		[ -z "$procs" ] || echo "         $procs"
	else
		printf '%-8s %-5s down %-45s  lease=%s\n' $N $DISP "(build=${BUILD#$ROOT/})" "${l:--}"
	fi
}

case $cmd in
status) if [ ${#A[@]} -gt 0 ]; then for n in "${A[@]}"; do status $n; done; else for n in $(awk -F'\t' '!/^#/ { print $1 }' $T); do status $n; done; fi
	echo "licensing port $PORT: pid $(port_pid)" ;;
env) load ${A[0]:?NAME}
	emit WINEPREFIX $WP; emit WINE_BUILD $BUILD; emit DISPLAY $DISP; emit INV_PREFIX prefixes/$N
	[ "$DRI" = - ] || emit DRI_PRIME $DRI ;;
lease) load ${A[0]:?NAME}; h=${A[1]:-$HOLDER}; [ -n "$h" ] || die "need HOLDER (arg, --holder or \$PREFIX_HOLDER)"
	exec 9>$L.lock; flock 9; o=$(lease_holder $N)
	[ -z "$o" ] || [ "$o" = "$h" ] || die "$N is leased to $o"
	{ grep -v "^$N " $L 2>/dev/null || true; echo "$N $h $(date +%Y-%m-%dT%H:%M)"; } >$L.new && mv $L.new $L ;;
release) load ${A[0]:?NAME}; h=${A[1]:-$HOLDER}
	exec 9>$L.lock; flock 9; o=$(lease_holder $N)
	[ -z "$o" ] || [ "$o" = "$h" ] || [ -n "$FORCE" ] || die "$N is leased to $o (pass HOLDER, or --force)"
	{ grep -v "^$N " $L 2>/dev/null || true; } >$L.new; mv $L.new $L ;;
stop) load ${A[0]:?NAME}; check_lease; sp=$(server_pid)
	[ -z "$sp" ] || [ "$(port_pid)" != "$sp" ] || [ -n "$FORCE" ] || die "$N holds the licensing port $PORT; --force to stop it (running Inventors lose licensing)"
	case $ROLE in licens*) [ -n "$FORCE" ] || die "$N is the licensing host; --force";; esac
	if [ -n "$sp" ]; then
		exe=$(readlink /proc/$sp/exe 2>/dev/null) || exe=$BUILD/server/wineserver
		WINEPREFIX=$WP $exe -k || true
		for _ in $(seq 50); do [ -d /proc/$sp ] || break; sleep 0.2; done
	fi
	left=$(prefix_pids); if [ -n "$left" ]; then echo "killing leftovers: $left"; kill $left 2>/dev/null || true; sleep 2; kill -9 $left 2>/dev/null || true; fi ;;
start) load ${A[0]:?NAME}; check_lease
	[ -z "$(server_pid)" ] || { echo "$N already running"; exit 0; }
	if ! DISPLAY=$DISP xset q >/dev/null 2>&1; then
		n=${DISP#:}
		if [ "$BUS" = - ]; then
			setsid nohup Xvfb $DISP -screen 0 1920x1080x24 >x/xvfb.$n.log 2>&1 </dev/null &
			until DISPLAY=$DISP xset q >/dev/null 2>&1; do sleep 0.2; done
		else
			setsid nohup x/start.sh $n $BUS >/dev/null 2>&1 </dev/null
			[ "$VNC" = - ] || x/vnc.sh $n
		fi
	fi
	export WINEPREFIX=$WP DISPLAY=$DISP WINEDLLOVERRIDES="mscoree,mshtml="
	[ "$DRI" = - ] || export DRI_PRIME=$DRI
	# services started by wineboot inherit our fds and outlive it: keep them off the caller's pipe
	"$BUILD/wine" wineboot -u >"x/wineboot.$N.log" 2>&1 </dev/null
	echo "$N started (log x/wineboot.$N.log)" ;;
*) die "unknown command $cmd" ;;
esac
