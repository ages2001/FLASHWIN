#!/usr/bin/env python3
"""Rebuild FPCPU.DLL (FP32 SSE2 gate) and FP10CPU.DLL (FP10 CPU mode)."""
import subprocess,sys
sys.path.insert(0,'/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/analysis')
from pad_legacy_pe import pad_image
import pefile
CC=['clang-19','--target=i686-w64-windows-gnu','-isystem','/usr/i686-w64-mingw32/include','-march=i386','-mno-sse','-mno-sse2','-ffreestanding','-fno-stack-protector','-fno-builtin','-Os','-c']
L='/usr/i686-w64-mingw32/lib/'
def dll(objs,deff,out,libs):
    subprocess.run(['lld-link-19','/nologo','/dll','/entry:DllMain@12','/subsystem:windows,3.10','/osversion:3.10','/nodefaultlib','/safeseh:no','/machine:x86','/def:'+deff,'/out:'+out,*objs,*[L+f'lib{x}.a' for x in libs]],check=True)
    pad_image(out);p=pefile.PE(out);p.OPTIONAL_HEADER.CheckSum=p.generate_checksum();p.write(out);p.close()
subprocess.run(CC+['fp32/compat/cpu_gate.c','-o','cpu_gate.obj'],check=True)
subprocess.run(CC+['compat/fp10_cpu_mode.c','-o','fp10_cpu_mode.obj'],check=True)
dll(['cpu_gate.obj'],'FPCPU.def','FPCPU.DLL',['kernel32','user32'])
dll(['fp10_cpu_mode.obj'],'FP10CPU.def','FP10CPU.DLL',['kernel32'])
print('ok')
