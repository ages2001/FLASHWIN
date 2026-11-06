#!/usr/bin/env python3
"""preload9x.py IN.EXE OUT.EXE

Adds a small entry-point prologue to an EXE: on Windows 95/98/ME it loads
FP9X.DLL from the EXE's own folder before the original entry point runs, so
the Unicode layer patches the EXE's imports. On Windows NT the prologue only
jumps to the original entry point. Position independent; existing imports are
kept and one KERNEL32 descriptor (GetVersion, GetModuleFileNameA,
LoadLibraryA) is added in the new section.
"""
import sys,struct,subprocess,tempfile,os,pefile
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
_m={'__name__':'unforwrd','__file__':os.path.join(os.path.dirname(os.path.abspath(__file__)),'UNFORWRD.PY')}
exec(compile(open(_m['__file__']).read(),_m['__file__'],'exec'),_m)
label_values,align=_m['label_values'],_m['align']
def build(src,dst):
    pe=pefile.PE(src);oh=pe.OPTIONAL_HEADER;last=pe.sections[-1]
    rva=align(last.VirtualAddress+max(last.Misc_VirtualSize,last.SizeOfRawData),oh.SectionAlignment)
    oep=oh.AddressOfEntryPoint
    a=['bits 32',f'org {rva}','start:',' call .pc','.pc: pop ebp',' call [ebp+iat_ver-.pc]',' test eax,eax',' jns .go',
       ' sub esp,272',' mov esi,esp',' push 260',' push esi',' push 0',' call [ebp+iat_gmfn-.pc]',
       ' test eax,eax',' jz .done',' cmp eax,250',' jae .done',' lea edi,[esi+eax]',
       '.b: cmp edi,esi',' je .cat',' cmp byte [edi-1],0x5c',' je .cat',' dec edi',' jmp .b',
       '.cat: lea ecx,[ebp+s_fp9x-.pc]','.c: mov al,[ecx]',' mov [edi],al',' inc ecx',' inc edi',' test al,al',' jnz .c',
       ' push esi',' call [ebp+iat_ll-.pc]','.done: add esp,272',
       f'.go: lea eax,[ebp+{oep}-start.pc]',' jmp eax','align 4']
    old=[]
    for d in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[]):
        s=d.struct;old.append((s.OriginalFirstThunk,s.TimeDateStamp,s.ForwarderChain,s.Name,s.FirstThunk))
    a.append('impdir:')
    for t in old:a.append(' dd '+','.join(str(x) for x in t))
    a+=[' dd ilt, 0, 0, s_k32, iat',' dd 0,0,0,0,0','ilt: dd h_ver, h_gmfn, h_ll, 0',
        'iat:','iat_ver: dd h_ver','iat_gmfn: dd h_gmfn','iat_ll: dd h_ll',' dd 0',
        'h_ver: dw 0',' db "GetVersion",0','align 2','h_gmfn: dw 0',' db "GetModuleFileNameA",0','align 2',
        'h_ll: dw 0',' db "LoadLibraryA",0','align 2','s_k32: db "KERNEL32.DLL",0','s_fp9x: db "FP9X.DLL",0','impend:']
    with tempfile.TemporaryDirectory() as t:
        asm=os.path.join(t,'x.asm');out=os.path.join(t,'x.bin');open(asm,'w').write('\n'.join(a)+'\n')
        r=subprocess.run(['nasm','-f','bin',asm,'-o',out],capture_output=True,text=True)
        if r.returncode:raise SystemExit(r.stderr)
        code=open(out,'rb').read()
    L=label_values(a,rva)
    data=bytearray(pe.__data__);data=data[:align(len(data),oh.FileAlignment)];off=len(data);raw=align(len(code),oh.FileAlignment)
    data+=code+b'\0'*(raw-len(code))
    pe2=pefile.PE(data=bytes(data));hdr=pe2.sections[-1].get_file_offset()+40
    if hdr+40>pe2.OPTIONAL_HEADER.SizeOfHeaders:raise SystemExit('no room for section header')
    data[hdr:hdr+40]=struct.pack('<8sIIIIIIHHI',b'.fp9x\0\0\0',len(code),rva,raw,off,0,0,0,0,0xE0000060)
    pe2=pefile.PE(data=bytes(data));pe2.FILE_HEADER.NumberOfSections+=1
    pe2.OPTIONAL_HEADER.SizeOfImage=align(rva+len(code),oh.SectionAlignment)
    d=pe2.OPTIONAL_HEADER.DATA_DIRECTORY;d[1].VirtualAddress=L['impdir'];d[1].Size=L['impend']-L['impdir'];d[11].VirtualAddress=0;d[11].Size=0
    pe2.OPTIONAL_HEADER.AddressOfEntryPoint=L['start']
    out=pe2.write();p3=pefile.PE(data=out);p3.OPTIONAL_HEADER.CheckSum=p3.generate_checksum();open(dst,'wb').write(p3.write())
if __name__=='__main__':build(sys.argv[1],sys.argv[2]);print('ok')
