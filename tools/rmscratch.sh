#!/usr/bin/env bash
# Delete scratch directories/files: tools/rmscratch.sh PATH...
# The only way workers delete things. Each PATH must resolve (symlinks followed for the parents) to
# something strictly below one of the scratch roots, and must not be a protected name. No globs: pass
# explicit paths. Refuses the whole call if any path is not allowed.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
ROOTS=("$ROOT/inst" "$ROOT/wt" "$ROOT/build-next" /tmp /var/tmp /dev/shm)
# frozen licensing build, regression baseline build, live worktrees of the main checkout
PROTECTED=("$ROOT/wt/lic" "$ROOT/wt/lic-build" "$ROOT/wt/regress-master" "$ROOT/wt/regress-master-build" "$ROOT/wt/wayland-inc")
[ $# -gt 0 ] || { echo "usage: $0 PATH..." >&2; exit 2; }
ok=()
for a in "$@"; do
	p=$(realpath -m -- "$(dirname -- "$a")")/$(basename -- "$a")   # the path itself may be a symlink: remove the link
	allowed=
	for r in "${ROOTS[@]}"; do case $p in "$r"/?*) allowed=1;; esac; done
	[ -n "$allowed" ] || { echo "rmscratch: refused, not below a scratch root: $a -> $p" >&2; exit 1; }
	for x in "${PROTECTED[@]}"; do case $p in "$x"|"$x"/*) echo "rmscratch: refused, protected: $p" >&2; exit 1;; esac; done
	if [ -e "$p/.git" ]; then echo "rmscratch: refused, a git worktree (use git worktree remove): $p" >&2; exit 1; fi
	ok+=("$p")
done
for p in "${ok[@]}"; do rm -rf -- "$p"; echo "removed $p"; done
