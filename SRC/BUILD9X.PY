#!/usr/bin/env python3
"""Build FP9X.DLL from fp9x.c: generates the export .def and the patch table."""
import re,subprocess,os,sys
H=os.path.dirname(os.path.abspath(__file__));OUT=sys.argv[1] if len(sys.argv)>1 else os.path.join(H,'out')
os.makedirs(OUT,exist_ok=True)
src=open(os.path.join(H,'FP9X.C')).read()
SKIP={'dispatchW','dispatchDlg','TranslateMessage_unused'}
fns=[]
for m in re.finditer(r'^\w[\w ]*?\bWINAPIV?\s+x9_(\w+)\s*\(([^)]*)\)\s*\{',src,re.M):
    n=m.group(1)
    if n in SKIP or n in [f for f,_ in fns]:continue
    args=m.group(2).strip()
    v='WINAPIV' in m.group(0)
    nb=0 if args in('void','') else 4*len([a for a in args.split(',')])
    fns.append((n,None if v else nb))
tab=open(os.path.join(OUT,'x9tab.c'),'w')
tab.write('typedef struct{const char*name;void*fn;}Entry;\n')
for n,nb in fns: tab.write(f'extern char x9_{n} __asm__("_x9_{n}'+('' if nb is None else f'@{nb}')+'");\n')
tab.write('const Entry x9_table[]={\n'+''.join(f' {{"{n}",&x9_{n}}},\n' for n,_ in fns)+' {0,0}};\n');tab.close()
d=open(os.path.join(OUT,'FP9X.def'),'w');d.write('LIBRARY FP9X.DLL\nEXPORTS\n')
for n,nb in fns: d.write((f' {n}=x9_{n}' if nb is None else f' {n}=_x9_{n}@{nb}')+'\n')
d.close()
CC=['clang-19','--target=i686-w64-windows-gnu','-isystem','/usr/i686-w64-mingw32/include','-march=i386','-mno-sse','-mno-sse2','-ffreestanding','-fno-stack-protector','-fno-builtin','-Os','-Wall','-Wno-unused-function','-c']
for f,o in((os.path.join(H,'FP9X.C'),'fp9x.o'),(os.path.join(OUT,'x9tab.c'),'x9tab.o')):
    subprocess.check_call(CC+[f,'-o',os.path.join(OUT,o)])
L='/usr/i686-w64-mingw32/lib/'
libs=[L+f'lib{x}.a' for x in('kernel32','user32','gdi32','advapi32','shell32','version','winmm','comdlg32')]
subprocess.check_call(['lld-link-19','/nologo','/dll','/entry:DllMain@12','/subsystem:windows,3.10','/osversion:3.10','/nodefaultlib','/safeseh:no','/machine:x86',
 '/def:'+os.path.join(OUT,'FP9X.def'),'/out:'+os.path.join(OUT,'FP9X.DLL'),os.path.join(OUT,'fp9x.o'),os.path.join(OUT,'x9tab.o')]+libs)
sys.path.insert(0,'/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/analysis')
import pefile
from pad_legacy_pe import pad_image
p=os.path.join(OUT,'FP9X.DLL')
try: pad_image(p)
except Exception as e: print('pad:',e)
pe=pefile.PE(p);pe.OPTIONAL_HEADER.CheckSum=pe.generate_checksum();pe.write(p)
print('FP9X.DLL',len(fns),'exports')
