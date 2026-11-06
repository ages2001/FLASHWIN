#!/usr/bin/env python3
"""Make the release trees (stage9x) from the verified NT trees (stage):
every proxy DLL gets its forwarders turned into lazy stubs (unforward.py),
FP9X.DLL is added, and the FP32 launcher gets the 9x prologue (preload9x.py).
The players run unchanged on Windows NT; on Windows 95/98/ME FP9X.DLL is used."""
import os,shutil,subprocess,sys
H=os.path.dirname(os.path.abspath(__file__))
SRC='/home/claude/stage';DST='/home/claude/stage9x'
shutil.rmtree(DST,ignore_errors=True)
for prod in ('FP10','FP32'):
    shutil.copytree(os.path.join(SRC,prod),os.path.join(DST,prod),ignore=shutil.ignore_patterns('*.orig'))
    for r,ds,fs in os.walk(os.path.join(DST,prod)):
        for f in fs:
            p=os.path.join(r,f)
            if f.upper().endswith('.DLL'):
                subprocess.run([sys.executable,os.path.join(H,'UNFORWRD.PY'),p,p+'.new'],check=True,capture_output=True);os.replace(p+'.new',p)
    shutil.copy(os.path.join(H,'FP9X.DLL'),os.path.join(DST,prod,'FP9X.DLL'))
l=os.path.join(DST,'FP32','FP32.EXE')
subprocess.run([sys.executable,os.path.join(H,'PRELOAD9.PY'),os.path.join(SRC,'FP32','FP32.EXE'),l],check=True)
print('ok')
