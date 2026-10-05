#!/bin/bash
# ac.sh DISPLAY LUA...: run Lua in the awesome started by wm.sh on DISPLAY (awesome-client).
# `ac.sh :100 clients` lists the clients: X window, name, tags, type, transient_for, flags.
W=$(cd "$(dirname "$0")/../.." && pwd); D=$1; shift
eval "$($W/tools/sysroot.sh env)"
export DISPLAY=$D DBUS_SESSION_BUS_ADDRESS=$(cat $W/inst/175/dbus.${D#:})
if [ "$1" = clients ]; then
  set -- 'local s = "" for _, c in ipairs(client.get()) do local t = {} for _, x in ipairs(c:tags()) do t[#t+1] = x.name end
  s = s .. string.format("0x%x %-12s tags %s type %s transient_for %s vis %s min %s hidden %s float %s ontop %s skip_tb %s bw %s geo %d,%d %dx%d op %s focus %s\n", c.window, c.name or "-", table.concat(t, ","), c.type,
    c.transient_for and string.format("0x%x", c.transient_for.window) or "-", tostring(c:isvisible()), tostring(c.minimized), tostring(c.hidden), tostring(c.floating), tostring(c.ontop), tostring(c.skip_taskbar),
    tostring(c.border_width), c.x, c.y, c.width, c.height, tostring(c.opacity), tostring(client.focus == c)) end return s'
fi
awesome-client "$@"
