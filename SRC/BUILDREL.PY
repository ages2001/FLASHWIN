#!/usr/bin/env python3
"""Build BIN/SETUP/FP10SET.EXE, FP32SET.EXE and BIN/PORTABLE/FP10PORT.EXE,
FP32PORT.EXE from the verified stage trees (/home/claude/stage/FP10, FP32).
WINSIMD is never bundled. Run with python3 -I."""
import struct,zlib,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,'/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/analysis')
from pad_legacy_pe import pad_image
import pefile
STAGE={'10':Path('/home/claude/stage9x/FP10'),'32':Path('/home/claude/stage9x/FP32')}
EXCLUDE={'WINSIMD.EXE','WINSIMD.SYS','WINSINFO.EXE'}
OUT=Path('/home/claude/deliverables/FLASHWIN/BIN')
def files(prod):
    st=STAGE[prod];out=[]
    for r,ds,fs in os.walk(st):
        ds.sort()
        for x in sorted(fs):
            if x.endswith('.orig') or x.upper() in EXCLUDE: continue
            rel=os.path.relpath(os.path.join(r,x),st).replace('/','\\')
            assert len(rel)<16,rel
            out.append((rel,Path(r,x).read_bytes()))
    # player EXE first for readability
    out.sort(key=lambda e:(0 if e[0].upper() in ('FP10.EXE','FP32.EXE') else 1,e[0]))
    return out
def pack(stub,fl):
    base=len(stub);off=len(fl)*28;table=b'';data=b''
    for n,raw in fl:
        table+=struct.pack('<16sIII',n.encode(),off,len(raw),zlib.crc32(raw));data+=raw;off+=len(raw)
    return stub+table+data+struct.pack('<III',0x31504b46,base,len(fl))
def cc(src,out,defs):
    subprocess.run(['clang-19','--target=i686-w64-windows-gnu','-isystem','/usr/i686-w64-mingw32/include','-march=i386','-mno-sse','-mno-sse2',
        '-ffreestanding','-fno-stack-protector','-fno-builtin','-Os',*defs,'-c',src,'-o',out],check=True)
def link(objs,out):
    L='/usr/i686-w64-mingw32/lib/'
    subprocess.run(['lld-link-19','/nologo','/entry:entry','/subsystem:windows,3.10','/osversion:3.10','/nodefaultlib','/safeseh:no','/machine:x86',
        *objs,L+'libkernel32.a',L+'libuser32.a','/out:'+out],check=True)
    pad_image(out);p=pefile.PE(out);p.OPTIONAL_HEADER.CheckSum=p.generate_checksum();p.write(out);p.close()
P=Path('/tmp/portbuild')
cc(str(P/'portrun.c'),str(P/'portrun10.obj'),['-DRUN_EXE="FP10.EXE"'])
cc(str(P/'portrun.c'),str(P/'portrun32.obj'),['-DRUN_EXE="APP\\\\FP32.EXE"','-DFP32_PORT'])
link([str(P/'portrun10.obj'),str(P/'porticon.obj')],str(P/'STUBP10.EXE'))
link([str(P/'portrun32.obj'),str(P/'porticon.obj')],str(P/'STUBP32.EXE'))
(OUT/'SETUP').mkdir(parents=True,exist_ok=True);(OUT/'PORTABLE').mkdir(parents=True,exist_ok=True)
for prod in ('10','32'):
    fl=files(prod)
    assert len(fl)<=32
    inst=pack(Path(f'/tmp/instbuild/STUB{prod}.EXE').read_bytes(),fl)
    (OUT/'SETUP'/f'FP{prod}SET.EXE').write_bytes(inst)
    port=pack((P/f'STUBP{prod}.EXE').read_bytes(),fl)
    (OUT/'PORTABLE'/f'FP{prod}PORT.EXE').write_bytes(port)
    man=[{'name':n,'bytes':len(r),'sha256':hashlib.sha256(r).hexdigest()} for n,r in fl]
    json.dump(man,open(f'/home/claude/deliverables/FLASHWIN/SRC/FP{prod}SET.JSN','w'),indent=1)
    print(prod,len(fl),'files', len(inst), len(port))
