"""Detect / fix PE images whose import thunks (IAT) live in a non-writable
section that the Windows 2000/XP loader would not unprotect (no IAT data
directory covering them). Fix = set DataDirectory[IAT] to the exact span of the
thunk arrays inside the read-only section."""
import pefile,sys
def sect_of(p,rva):
    for s in p.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData): return s
def analyze(path):
    p=pefile.PE(path)
    p.parse_data_directories(directories=[1])
    if not hasattr(p,'DIRECTORY_ENTRY_IMPORT'): return p,None,[]
    iat=p.OPTIONAL_HEADER.DATA_DIRECTORY[12]; impdd=p.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    imp_sec=sect_of(p,impdd.VirtualAddress)
    bad=[]
    for e in p.DIRECTORY_ENTRY_IMPORT:
        ft=e.struct.FirstThunk; n=len(e.imports); end=ft+4*(n+1)
        s=sect_of(p,ft); writable=bool(s.Characteristics&0x80000000)
        covered = iat.VirtualAddress and iat.VirtualAddress<=ft and end<=iat.VirtualAddress+iat.Size
        # NT5 without IAT dir unprotects the section holding the import directory
        unprot_by_nt5 = covered or (not iat.VirtualAddress and s is imp_sec)
        if not writable and not unprot_by_nt5: bad.append((e.dll.decode(),ft,end,s.Name.rstrip(b'\0').decode()))
    return p,iat,bad
def fix(path):
    p,iat,bad=analyze(path)
    if not bad: return False
    lo=min(b[1] for b in bad); hi=max(b[2] for b in bad)
    secs={b[3] for b in bad}; assert len(secs)==1, f"thunks in several RO sections {secs}"
    p.OPTIONAL_HEADER.DATA_DIRECTORY[12].VirtualAddress=lo
    p.OPTIONAL_HEADER.DATA_DIRECTORY[12].Size=hi-lo
    p.OPTIONAL_HEADER.CheckSum=p.generate_checksum()
    data=bytearray(p.write())
    open(path,'wb').write(data)
    return (lo,hi-lo,secs.pop(),len(bad))
if __name__=='__main__':
    mode=sys.argv[1]
    for f in sys.argv[2:]:
        if mode=='check':
            try:
                p,iat,bad=analyze(f)
                print(("BAD " if bad else "ok  ")+f.split('/')[-1], ("%d dlls in RO %s, iatdir=%x/%x"%(len(bad),bad[0][3],iat.VirtualAddress,iat.Size)) if bad else "")
            except Exception as ex: print("ERR",f,ex)
        else:
            print(f.split('/')[-1],'->',fix(f))
