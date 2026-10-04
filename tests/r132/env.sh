# source: sysroot + Wayland env + scratch prefix for the 132 probes (W = wine binary, default wt/132-build)
cd /home/xl0/projects/wine
eval "$(tools/sysroot.sh env)"
eval "$(x/wayland.sh env)"
export WINEPREFIX=/home/xl0/projects/wine/inst/132/pfx
export W=${W:-/home/xl0/projects/wine/wt/132-build/wine}
