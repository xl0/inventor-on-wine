# 112 COM server objects stay alive after all clients released them
Status: open (draft) · Owner: - · Branch: - · Found in: 110 worker (tests/r109/stress.c, R109_SAFE=3)

stress.c with no disconnects reports thousands of server objects still alive at exit on Wine
(before and after 110); Windows reports 0. A COM-side leak (stub managers / ext refs not dropped
on client release, or RemRelease lost?). May contribute to long-session growth (086/soaks), though
the VM shows Inventor itself grows as much. Find which references are kept and why; match Windows.
