#!/bin/bash
# xinfo.sh DISPLAY [PATTERN]: X side of the windows titled "r175 *" (or PATTERN): override-redirect, map state,
# visual depth, parent chain, WM_TRANSIENT_FOR, window type, _NET_WM_DESKTOP, WM_STATE, group / client leader,
# _NET_WM_STATE, opacity (175).
export DISPLAY=$1; PAT=${2:-r175 }
xwininfo -root -tree | grep "\"$PAT" | while read id rest; do
  name=$(echo "$rest" | sed 's/^"\([^"]*\)".*/\1/')
  info=$(xwininfo -id $id)
  ov=$(echo "$info" | awk '/Override Redirect/{print $4}'); map=$(echo "$info" | awk '/Map State/{print $3}')
  depth=$(echo "$info" | awk '/Depth:/{print $2}'); geo=$(echo "$info" | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width/{w=$2}/Height/{h=$2}END{print x","y" "w"x"h}')
  parent=$(xwininfo -id $id -children | awk '/Parent window id/{print $4}'); root=$(xwininfo -id $id -children | awk '/Root window id/{print $4}')
  p() { xprop -id $id $1 2>/dev/null | sed 's/^[^=#]*[=#] *//; s/window id # //' | grep -v 'not found\|no such atom' | tr '\n' ' ' | sed 's/ *$//'; }
  tf=$(p WM_TRANSIENT_FOR); type=$(p _NET_WM_WINDOW_TYPE | sed 's/_NET_WM_WINDOW_TYPE_//g'); desk=$(p _NET_WM_DESKTOP)
  st=$(xprop -id $id WM_STATE | awk '/window state/{print $3}'); nst=$(p _NET_WM_STATE | sed 's/_NET_WM_STATE_//g')
  grp=$(xprop -id $id WM_HINTS | awk '/window id # of group leader/{print $NF}'); lead=$(p WM_CLIENT_LEADER); op=$(p _NET_WM_WINDOW_OPACITY)
  [ "$parent" = "$root" ] && par=root || par="frame $parent"
  echo "$name: $id override=$ov map=$map depth=$depth geo=$geo parent=$par transient_for=${tf:--} type=${type:--} desktop=${desk:--} WM_STATE=${st:--} _NET_WM_STATE=[${nst}] group=${grp:--} leader=${lead:--} opacity=${op:--}"
done
