#!/usr/bin/env python3
"""Same as vm/input.py (click X Y | dclick X Y | key COMBO... | type TEXT), against the vmwl QMP socket."""
import os, runpy, sys
d = os.path.dirname(os.path.abspath(__file__))
os.environ['VM_DIR'] = d
sys.argv[0] = os.path.join(d, '..', 'vm', 'input.py')
runpy.run_path(sys.argv[0], run_name='__main__')
