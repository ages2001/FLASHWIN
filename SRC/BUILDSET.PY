#!/usr/bin/env python3
"""Build STUB10.EXE / STUB32.EXE (installer stubs) from setup.c."""
import subprocess,sys
sys.path.insert(0,'/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/analysis')
from pad_legacy_pe import pad_image
import pefile
L='/usr/i686-w64-mingw32/lib/'
for prod,defs in (('10',[]),('32',['-DFP32_SETUP'])):
    subprocess.run(['clang-19','--target=i686-w64-windows-gnu','-isystem','/usr/i686-w64-mingw32/include','-march=i386','-mno-sse','-mno-sse2','-ffreestanding','-fno-stack-protector','-fno-builtin','-Os',*defs,'-c','setup.c','-o',f'setup{prod}.obj'],check=True)
    out=f'STUB{prod}.EXE'
    subprocess.run(['lld-link-19','/nologo','/entry:entry','/subsystem:windows,3.10','/osversion:3.10','/nodefaultlib','/safeseh:no','/machine:x86',f'setup{prod}.obj','icons.obj',
        *[L+f'lib{x}.a' for x in ('kernel32','user32','gdi32','advapi32')],'/out:'+out],check=True)
    pad_image(out);p=pefile.PE(out);p.OPTIONAL_HEADER.CheckSum=p.generate_checksum();p.write(out);p.close();print(out)
