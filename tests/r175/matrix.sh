#!/bin/bash
# matrix.sh TAG SO N [CONFIG...]: scen.sh N under each WM config (awesome, awesome+picom, openbox, openbox+picom) on $D
# with winex11.so replaced by SO (A/B of driver builds in one Wine build); results in inst/175/TAG-CONFIG.txt (175).
. "$(dirname "$0")/env.sh"
tag=$1; so=$2; N=$3; shift 3
for c in ${@:-awesome awesome+picom openbox openbox+picom}; do
  $B/server/wineserver -k 2>/dev/null; sleep 1
  cp --remove-destination $so $B/dlls/winex11.drv/winex11.so
  $T/wm.sh $DISPLAY ${c%+*} $([ "$c" != "${c%+picom}" ] && echo picom) > /dev/null 2>&1
  [ "${c%+*}" = awesome ] && $T/ac.sh $DISPLAY 'require("gears").wallpaper.set("#404040")' > /dev/null
  MOVE=$([ "${c%+*}" = awesome ] && echo awesome) SETTLE=$([ "$c" != "${c%+picom}" ] && echo 2.5 || echo 1) $T/scen.sh $N > $S/$tag-$c.txt 2>&1
  echo "$tag $c: $(grep -c "(BAD)" $S/$tag-$c.txt) lines with owned windows left behind"
done
$B/server/wineserver -k 2>/dev/null
