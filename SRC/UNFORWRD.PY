#!/usr/bin/env python3
"""unforward.py IN.DLL OUT.DLL

Windows 95/98/ME do not resolve forwarded exports ("winmm.waveInOpen"), so a
proxy DLL built from forwarders cannot load there. This tool rewrites every
forwarded export of a PE file into a small position-independent stub in a new
section. Each stub resolves its target lazily on first call:

  * on Windows 9x, FP9X.DLL (the Unicode/9x layer) is asked first, so a stub
    for e.g. CreateWindowExW reaches a working implementation instead of the
    non-functional 9x export;
  * otherwise (and on NT) the original forwarder target is used
    (LoadLibraryA + GetProcAddress, by name or "#ordinal");
  * if neither exists, a stub that returns 0 and removes the stdcall
    arguments is used, so the caller keeps a balanced stack.

The resolved address is cached; later calls are one indirect jump. The stub
clobbers only EAX/ECX/EDX, which are free at entry of any stdcall/cdecl API.
A new import descriptor (KERNEL32: LoadLibraryA, GetProcAddress, GetVersion)
is added; existing descriptors are kept unchanged. No relocations are needed.
"""
import sys, struct, json, subprocess, tempfile, os, pefile

ARG = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ARGBYTES.JSN')))
CDECL_DLLS = {'CRTDLL', 'MSVCRT', 'NTDLL_CDECL'}

def align(v, a): return (v + a - 1) // a * a

def build(src, dst):
    pe = pefile.PE(src)
    if not hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        open(dst, 'wb').write(open(src, 'rb').read()); return 0
    fwd = []
    exp = pe.DIRECTORY_ENTRY_EXPORT
    for e in exp.symbols:
        if e.forwarder:
            dll, fn = e.forwarder.decode().split('.', 1)
            fwd.append((e.ordinal, e.name.decode() if e.name else None, dll, fn))
    if not fwd:
        open(dst, 'wb').write(open(src, 'rb').read()); return 0
    oh = pe.OPTIONAL_HEADER
    salign, falign = oh.SectionAlignment, oh.FileAlignment
    last = pe.sections[-1]
    rva = align(last.VirtualAddress + max(last.Misc_VirtualSize, last.SizeOfRawData), salign)
    # ---- assemble the section ----
    a = [f'bits 32', f'org {rva}']
    # stubs
    for i, (o, n, dll, fn) in enumerate(fwd):
        a += [f'stub{i}:', f' call .s', f'.s: pop eax', f' mov eax,[eax+slot{i}-.s]',
              f' test eax,eax', f' jz .t', f' jmp eax', f'.t: push {i}', f' jmp fp9xres']
    for i, (o, n, dll, fn) in enumerate(fwd):
        name = fn[1:] if fn.startswith('#') else fn
        nb = 0 if dll.upper() in CDECL_DLLS else ARG.get(name, 0)
        a += [f'miss{i}:', ' xor eax,eax', f' ret {nb}' if nb else ' ret']
    a += ['fp9xres:',
          ' pushad',
          ' call .pc', '.pc: pop ebp',
          ' mov esi,[esp+32]',
          ' cmp dword [ebp+inited-.pc],0',
          ' jne .ready',
          ' call [ebp+iat_ver-.pc]',
          ' test eax,eax', ' jns .nt',
          ' lea eax,[ebp+s_fp9x-.pc]', ' push eax', ' call [ebp+iat_ll-.pc]',
          ' mov [ebp+h9x-.pc],eax',
          '.nt: mov dword [ebp+inited-.pc],1',
          '.ready:',
          ' lea edi,[ebp+table-.pc]',
          ' lea edi,[edi+esi*8]',          # entry: dd dllname_off, dd funcname_off (from table)
          ' xor ebx,ebx',
          ' mov eax,[ebp+h9x-.pc]', ' test eax,eax', ' jz .native',
          ' mov ecx,[edi+4]', ' test ecx,0x80000000', ' jnz .native',   # ordinal: no 9x layer
          ' lea ecx,[ebp+ecx+table-.pc]', ' push ecx', ' push eax', ' call [ebp+iat_gpa-.pc]',
          ' mov ebx,eax', ' test eax,eax', ' jnz .got',
          '.native:',
          ' mov ecx,[edi]', ' lea ecx,[ebp+ecx+table-.pc]', ' push ecx', ' call [ebp+iat_ll-.pc]',
          ' test eax,eax', ' jz .miss',
          ' mov ecx,[edi+4]', ' test ecx,0x80000000', ' jz .byname',
          ' and ecx,0xffff', ' jmp .call',
          '.byname: lea ecx,[ebp+ecx+table-.pc]',
          '.call: push ecx', ' push eax', ' call [ebp+iat_gpa-.pc]',
          ' mov ebx,eax', ' test eax,eax', ' jnz .got',
          '.miss:',
          ' lea ebx,[ebp+misstab-.pc]', ' mov ebx,[ebx+esi*4]', ' add ebx,ebp',
          '.got:',
          ' lea ecx,[ebp+slots-.pc]', ' mov [ecx+esi*4],ebx',
          ' mov [esp+32],ebx',
          ' popad',
          ' ret',
          'align 4']
    a += ['inited: dd 0', 'h9x: dd 0', 'slots:'] + [f'slot{i}: dd 0' for i in range(len(fwd))]
    a += ['misstab:'] + [f' dd miss{i}-fp9xres.pc' for i in range(len(fwd))]
    # table: per entry (dll string offset, func string offset | 0x80000000+ordinal), offsets relative to 'table'
    a += ['table:'] + [f' dd d{i}-table, ' + (f'0x80000000+{int(fn[1:])}' if fn.startswith('#') else f'f{i}-table') for i, (o, n, dll, fn) in enumerate(fwd)]
    for i, (o, n, dll, fn) in enumerate(fwd):
        a.append(f'd{i}: db "{dll}.DLL",0')
        if not fn.startswith('#'): a.append(f'f{i}: db "{fn}",0')
    a.append('s_fp9x: db "FP9X.DLL",0')
    # import directory: old descriptors + kernel32 descriptor
    a.append('align 4')
    old = []
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            s = d.struct
            old.append((s.OriginalFirstThunk, s.TimeDateStamp, s.ForwarderChain, s.Name, s.FirstThunk))
    a.append('impdir:')
    for t in old: a.append(' dd ' + ','.join(str(x) for x in t))
    a += [' dd ilt, 0, 0, s_k32, iat', ' dd 0,0,0,0,0',
          'ilt: dd h_ll, h_gpa, h_ver, 0',
          'iat:', 'iat_ll: dd h_ll', 'iat_gpa: dd h_gpa', 'iat_ver: dd h_ver', ' dd 0',
          'h_ll: dw 0', ' db "LoadLibraryA",0', 'align 2',
          'h_gpa: dw 0', ' db "GetProcAddress",0', 'align 2',
          'h_ver: dw 0', ' db "GetVersion",0', 'align 2',
          's_k32: db "KERNEL32.DLL",0', 'impend:']
    with tempfile.TemporaryDirectory() as t:
        asm = os.path.join(t, 'x.asm'); out = os.path.join(t, 'x.bin')
        # fix: 'common.pc' is a local label of 'common' -> nasm name is common.pc
        open(asm, 'w').write('\n'.join(a) + '\n')
        r = subprocess.run(['nasm', '-f', 'bin', asm, '-o', out], capture_output=True, text=True)
        if r.returncode: raise SystemExit(r.stderr[:2000])
        code = open(out, 'rb').read()
        lst = os.path.join(t, 'x.map')
        # find label addresses via a second pass with a symbol dump
        open(asm, 'a').write('')
        r = subprocess.run(['nasm', '-f', 'bin', asm, '-o', out, '-l', lst], capture_output=True, text=True)
    # locate offsets: assemble again emitting label values
    labels = label_values(a, rva)
    stubs = [labels[f'stub{i}'] for i in range(len(fwd))]
    # ---- append section ----
    raw = align(len(code), falign)
    data = bytearray(pe.__data__)
    data = data[:align(len(data), falign)]
    newsec_off = len(data)
    data += code + b'\0' * (raw - len(code))
    pe2 = pefile.PE(data=bytes(data))
    # section header
    hdr_off = pe2.sections[-1].get_file_offset() + 40
    if hdr_off + 40 > pe2.OPTIONAL_HEADER.SizeOfHeaders: raise SystemExit('no room for section header')
    sh = struct.pack('<8sIIIIIIHHI', b'.fp9x\0\0\0', len(code), rva, raw, newsec_off, 0, 0, 0, 0, 0xE0000060)
    data[hdr_off:hdr_off + 40] = sh
    pe2 = pefile.PE(data=bytes(data))
    pe2.FILE_HEADER.NumberOfSections += 1
    pe2.OPTIONAL_HEADER.SizeOfImage = align(rva + len(code), salign)
    imp = pe2.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    imp.VirtualAddress = labels['impdir']; imp.Size = labels['impend'] - labels['impdir']
    pe2.OPTIONAL_HEADER.DATA_DIRECTORY[11].VirtualAddress = 0; pe2.OPTIONAL_HEADER.DATA_DIRECTORY[11].Size = 0  # bound imports
    # export address table entries
    e2 = pe2.DIRECTORY_ENTRY_EXPORT.struct
    for i, (o, n, dll, fn) in enumerate(fwd):
        idx = o - e2.Base
        pe2.set_dword_at_rva(e2.AddressOfFunctions + idx * 4, stubs[i])
    pe2.OPTIONAL_HEADER.CheckSum = 0
    out = pe2.write()
    pe3 = pefile.PE(data=out); pe3.OPTIONAL_HEADER.CheckSum = pe3.generate_checksum()
    open(dst, 'wb').write(pe3.write())
    return len(fwd)

def label_values(lines, org):
    """Assemble with a trailer that prints label values via nasm %warning."""
    names = [l[:-1] for l in lines if l.endswith(':') and not l.startswith('.') and ' ' not in l]
    names += [l.split(':')[0] for l in lines if ':' in l and not l.startswith((' ', '.')) and not l.endswith(':')]
    names = list(dict.fromkeys(n for n in names if n.isidentifier()))
    trailer = [f'dd {n}' for n in names]
    with tempfile.TemporaryDirectory() as t:
        asm = os.path.join(t, 'y.asm'); out = os.path.join(t, 'y.bin')
        open(asm, 'w').write('\n'.join(lines + ['lbl_tail:'] + trailer) + '\n')
        r = subprocess.run(['nasm', '-f', 'bin', asm, '-o', out], capture_output=True, text=True)
        if r.returncode: raise SystemExit(r.stderr[:2000])
        b = open(out, 'rb').read()
    vals = struct.unpack_from(f'<{len(names)}I', b, len(b) - 4 * len(names))
    return dict(zip(names, vals))

if __name__ == '__main__':
    n = build(sys.argv[1], sys.argv[2])
    print(f'{os.path.basename(sys.argv[1])}: {n} forwarders converted')
