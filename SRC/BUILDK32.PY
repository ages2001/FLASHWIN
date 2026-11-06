import sys
from pathlib import Path
sys.path.insert(0,'/home/claude/fp32build')
import buildgaps2 as b
b.B=Path('/tmp/apc/def'); b.OUTDIR=Path('/tmp/apc/out')
g=b.GAPS['F32K']; apis=dict(g[1]); apis['QueueUserAPC']=('QueueUserAPC2',12)
b.GAPS['F32K']=(g[0]+['apc'],apis,g[2])
print(b.rebuild('F32K'))
