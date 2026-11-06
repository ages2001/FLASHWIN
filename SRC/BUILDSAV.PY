#!/usr/bin/env python3
"""Rebuild FPK (save adapter), FPS (SHGetFolderPath bridge) and FPG (no
diagnostic files) for FP10, reusing buildfp10gaps.rebuild(). Run with -I."""
import sys,subprocess,shutil
from pathlib import Path
sys.path.insert(0,'/home/claude/fp10build')
import buildfp10gaps as b
W=Path('/tmp/fp10save');OBJ=W/'obj';C=Path('/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/compat')
def cc(src,out,flags=()):
    subprocess.run(['clang-19','--target=i686-w64-windows-gnu','-isystem','/usr/i686-w64-mingw32/include','-I','/tmp/fpcpu/compat','-I',str(C),'-march=i386','-mno-sse','-mno-sse2',
        '-ffreestanding','-fno-stack-protector','-fno-builtin','-Os',*flags,'-c',str(src),'-o',str(out)],check=True)
cc(W/'kernel.c',OBJ/'kernel.obj',['-DFP_DYNAMIC_CPU'])
cc(W/'fp10_save.c',OBJ/'fp10_save.obj')
cc(W/'gap_shell10.c',OBJ/'gap_shell10.obj'); cc(W/'gap_shell3.c',OBJ/'gap_shell3.obj')
cc(W/'gdi.c',OBJ/'gdi.obj',['-DFP_SOFTWARE_RGB'])
cc(W/'apc.c',OBJ/'apc.obj')
cc(C/'trace.c',OBJ/'trace-gdi.obj',['-DFP_GDI','-DFP_OPTIONAL_TRACE'])
# stage: base objs dir = copy of fp10build objs with replacements
B=W/'base';B.mkdir(exist_ok=True)
for o in Path('/home/claude/fp10build').glob('*.obj'): shutil.copy(o,B/o.name)
for o in ('kernel','fp10_save','gdi','trace-gdi','apc'): shutil.copy(OBJ/f'{o}.obj',B/f'{o}.obj')
G=W/'gapobj';G.mkdir(exist_ok=True)
for o in Path('/home/claude/fp32build').glob('gap_*.obj'): shutil.copy(o,G/o.name)
shutil.copy(OBJ/'gap_shell10.obj',G/'gap_shell10.obj'); shutil.copy(OBJ/'gap_shell3.obj',G/'gap_shell3.obj')
b.COMPAT=W/'compat'; b.FP10=B; b.GAPOBJ=G; b.OUT=W/'out'; b.OUT.mkdir(exist_ok=True)
k=b.GAPS['FPK']; apis=dict(k[2]); apis['QueueUserAPC']=('QueueUserAPC2',12)
b.GAPS['FPK']=(k[0]+['fp10_save','apc'],k[1],apis)+k[3:]
fps=b.GAPS['FPS']; b.GAPS['FPS']=(fps[0],['gap_shell10','gap_shell2','gap_shell3'])+fps[2:]
for stem in ('FPK','FPS','FPG'): print(b.rebuild(stem))
