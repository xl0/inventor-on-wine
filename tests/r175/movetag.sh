#!/bin/bash
# movetag.sh DISPLAY NAME TAG: awesome: move the client titled NAME to tag number TAG (like Mod4+Shift+N on the focused client)
# movetag.sh DISPLAY view TAG: view tag TAG only
here=$(dirname "$0")
if [ "$2" = view ]; then exec $here/ac.sh $1 "screen[1].tags[$3]:view_only()"; fi
exec $here/ac.sh $1 "for _, c in ipairs(client.get()) do if c.name == '$2' then c:move_to_tag(screen[1].tags[$3]) end end"
