#!/bin/bash
# chain.sh NAME: verification chains of the display-lock version, one per display
cd /home/xl0/projects/wine/inst/182
case $1 in
c1480) ./dcchurn.sh fix 1480 2 20; ./p173.sh fix 1480 ;;
c1481) ./dcchurn.sh fix 1481 4 20; WINEDEBUG=err+all,+synchronous ./gs.sh fix 1481 syncn 6 20 6 n; WINEDEBUG=err+all,+synchronous ./gs.sh fix 1481 sync 6 20 6 ;;
c1485) ./dcchurn.sh fix 1485 8 20; ./gs.sh fix 1485 own 10 20 6; ./gs.sh fix 1485 own8 5 30 12 ;;
c1483) ./gs.sh fix 1483 ob 10 20 6; SEEDS="4 5 6" ./p173.sh fix 1483; ./vs.sh fix 1483 6 ;;
c1484) ./vs.sh fix 1484 6 ;;
d1486) ./dbg.sh 1486 ;;
d1482) ./dbg.sh 1482 ;;
esac
