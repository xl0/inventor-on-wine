#!/usr/bin/env bash
# Project-local package prefix (deps/sysroot, git-ignored) of Ubuntu archive packages that the host
# does not have: -dev headers, mingw-w64, bison, Xvfb, ... No root: packages are unpacked, not installed.
# Usage: tools/sysroot.sh add PKG...      resolve the not-installed closure (apt-get -s), download it
#                                         pinned to the exact versions, unpack, record in sysroot.pkgs
#        tools/sysroot.sh rebuild         recreate the prefix from sysroot.pkgs (.debs kept in deps/sysroot-debs);
#                                         wipes it first: never while builds or tests run
#        tools/sysroot.sh env             export lines for eval; empty if the prefix is absent
#        tools/sysroot.sh run CMD...      run CMD with that environment
# sysroot.pkgs: "name=version sha256-of-deb" per line, sorted. The prefix has /usr paths baked in
# (.pc files are rewritten to point into it), so it is not relocatable: rebuild after moving the tree.
set -euo pipefail
shopt -s nullglob extglob
root=$(cd "$(dirname "$0")/.." && pwd)
S=$root/deps/sysroot D=$root/deps/sysroot-debs L=$root/tools/sysroot.pkgs
MA=x86_64-linux-gnu

deb_of() { # NAME VERSION -> path of the downloaded .deb (epoch colon is %3a in the file name)
	local v=${2//:/%3a} f
	for f in "$D/$1_${v}_"*.deb; do [ -e "$f" ] && echo "$f"; return 0; done
}

fetch() { # NAME VERSION: download if missing
	[ -n "$(deb_of "$1" "$2")" ] || (cd "$D" && apt-get download "$1=$2" > /dev/null < /dev/null)
}

fixup() {
	# -dev packages ship libfoo.so -> libfoo.so.N, relative; the target is in the runtime package that
	# is installed system-wide, so the link dangles in the prefix (ld: cannot find -lfoo). Repoint it
	# at the system file. Absolute links (alternatives, /usr/lib/gcc/...) get the prefix file instead.
	local l t a
	while IFS= read -r -d '' l; do
		t=$(readlink "$l")
		case $t in /*) a=$t;; *) a=$(dirname "${l#"$S"}")/$t;; esac
		a=$(realpath -m "$a")
		if [ -e "$a" ]; then ln -sfn "$a" "$l"
		elif [ -e "$S$a" ] || [ -L "$S$a" ]; then ln -sfn "$S$a" "$l"
		elif [[ $a != /usr/share/@(doc|man)/* ]]; then echo "sysroot: dangling ${l#"$S"} -> $t" >&2; fi
	done < <(find "$S" -xtype l -print0)
	# The postinst of gcc-mingw-w64-* makes x86_64-w64-mingw32-gcc etc. alternatives. Thread model:
	# the win32 variant (priority 60) beats posix (30) in Debian's auto mode, so link every *-win32.
	local f
	for f in "$S"/usr/bin/*-w64-mingw32-*-win32; do ln -sfn "$(basename "$f")" "${f%-win32}"; done
	# libblas3/liblapack3 are reached through alternatives (libblas.so.3 -> blas/libblas.so.3);
	# without the link numpy fails with "import numpy from its source directory".
	for f in "$S/usr/lib/$MA"/{blas,lapack}/lib*.so.3; do ln -sfn "${f#"$S/usr/lib/$MA/"}" "$S/usr/lib/$MA/$(basename "$f")"; done
	# pkg-config: .pc files say prefix=/usr; without this -I/-L point at /usr and miss the prefix.
	# (PKG_CONFIG_SYSROOT_DIR would also prefix -I/-L of the system's own .pc files.)
	find "$S/usr" -name '*.pc' -type f -exec sed -i -E "s#(^|[ =])/usr(/|\$)#\\1$S/usr\\2#g" {} +
}

# Unpack every .deb of the list into a fresh prefix.
unpack() {
	local n v s f
	"$root/tools/del" -rf "$S"; mkdir -p "$S"
	while read -r n v s; do dpkg-deb -x "$(deb_of "${n%%=*}" "${n#*=}")" "$S"; done < "$L"
	fixup
}

# List line for each package: download (if needed), check the recorded hash, or record it.
sync_list() { # reads "name=version [sha]" lines on stdin, writes the sorted list to $L
	local n v s f h out=()
	mkdir -p "$D"
	while read -r n s; do
		fetch "${n%%=*}" "${n#*=}"
		f=$(deb_of "${n%%=*}" "${n#*=}"); h=$(sha256sum "$f" | cut -d' ' -f1)
		[ -z "$s" ] || [ "$s" = "$h" ] || { echo "sysroot: sha256 mismatch for $n" >&2; exit 1; }
		out+=("$n $h")
	done
	printf '%s\n' "${out[@]}" | sort -u > "$L.tmp"; mv "$L.tmp" "$L"
}

case ${1:-} in
add)
	shift
	inst=$(apt-get -s install --no-install-recommends "$@")
	! grep -q '^Inst [^ ]* \[' <<< "$inst" || { echo "sysroot: would upgrade installed packages:" >&2; grep '^Inst [^ ]* \[' <<< "$inst" >&2; exit 1; }
	new=$(sed -En 's/^Inst ([^ ]+) \(([^ ]+) .*/\1=\2/p' <<< "$inst")
	# keep recorded entries, except old versions of the packages being added
	{ [ ! -f "$L" ] || awk -F'[= ]' 'NR == FNR { skip[$1] = 1; next } !($1 in skip)' <(sed 's/=.*//' <<< "$new") "$L"; echo "$new"; } | sync_list
	# only the new packages: the prefix stays usable for running builds and tests (rebuild wipes it)
	mkdir -p "$S"; for n in $new; do dpkg-deb -x "$(deb_of "${n%%=*}" "${n#*=}")" "$S"; done
	fixup;;
rebuild)
	sync_list < "$L"
	unpack;;
env)
	[ -d "$S/usr" ] || exit 0
	cat <<EOF
if [ "\${SYSROOT_ENV:-}" != "$S" ]; then
export SYSROOT_ENV="$S"
export PATH="\$PATH:$S/usr/bin"
export PKG_CONFIG_PATH="$S/usr/lib/$MA/pkgconfig:$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
export CPATH="$S/usr/include:$S/usr/include/$MA\${CPATH:+:\$CPATH}"
export LIBRARY_PATH="$S/usr/lib/$MA:$S/usr/lib\${LIBRARY_PATH:+:\$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$S/usr/lib/$MA:$S/usr/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export PYTHONPATH="$S/usr/lib/python3/dist-packages\${PYTHONPATH:+:\$PYTHONPATH}"
export BISON_PKGDATADIR="$S/usr/share/bison"
export M4="$S/usr/bin/m4"
export XDG_DATA_DIRS="\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}:$S/usr/share"
export XDG_CONFIG_DIRS="\${XDG_CONFIG_DIRS:-/etc/xdg}:$S/etc/xdg"
export GI_TYPELIB_PATH="$S/usr/lib/$MA/girepository-1.0\${GI_TYPELIB_PATH:+:\$GI_TYPELIB_PATH}"
export LUA_PATH="$S/usr/share/lua/5.3/?.lua;$S/usr/share/lua/5.3/?/init.lua;$S/usr/share/awesome/lib/?.lua;$S/usr/share/awesome/lib/?/init.lua;;"
export LUA_CPATH="$S/usr/lib/$MA/lua/5.3/?.so;;"
fi
EOF
	;;
run)
	shift
	eval "$("$0" env)"
	exec "$@";;
*)
	sed -n '2,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 1;;
esac
