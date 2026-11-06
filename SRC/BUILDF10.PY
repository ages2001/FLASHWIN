#!/usr/bin/env python3
"""Close all FP10 gaps except QueueUserAPC's functionality (present, non-functional).
Reuses the FP32 gap objects (same gnu target) plus one FP10-only shell extra.
Each proxy keeps its FP* LIBRARY name. clang-19 objs + lld-link-19 + pad_image.
Run with -I."""
import sys, re, subprocess, pefile
from pathlib import Path
sys.path.insert(0,"/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/analysis")
from pad_legacy_pe import pad_image

COMPAT=Path("/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/compat")
FP10=Path("/home/claude/fp10build")          # shipped base objs
GAPOBJ=Path("/home/claude/fp32build")        # reused gap objs
LIB=Path("/usr/i686-w64-mingw32/lib")
SYSLIBS=[str(LIB/f"lib{n}.a") for n in ("kernel32","user32","gdi32","advapi32")]
OUT=FP10/"gaps"; OUT.mkdir(exist_ok=True)

# proxy -> (base_objs, gap_objs, {export:(impl,argbytes)}, extra_libs)
def same(d): return {k:(k,v) for k,v in d.items()}
GAPS={
 "FPC":(["trace"],["gap_crypt"], same({"CryptGetMessageCertificates":20,"CertCreateCertificateContext":12,
        "CertFindCertificateInStore":24,"CertVerifySubjectCertificateContext":12,"CertFreeCertificateContext":4,
        "CertCloseStore":8,"CryptVerifyMessageSignature":28}),[]),
 "FPCLR":(["trace"],["gap_mscms"], same({"CreateColorTransformW":16,"CloseColorProfile":4,
        "TranslateBitmapBits":44,"DeleteColorTransform":4,"OpenColorProfileW":16}),[]),
 "FPD":(["trace"],["gap_dsound"], {"ordinal8":("dsOrdinal8",8)},[]),
 "FPG":(["trace-gdi","gdi"],["gap_gdi"], same({"GetICMProfileA":12,"EnumFontFamiliesExW":20}),[]),
 "FPK":(["trace","kernel","locale","thread_affinity","number_format"],["gap_kernel","gap_kernel2","gap_fiber"],
        same({"QueueUserAPC":12,"EnumSystemLocalesW":8,"CancelWaitableTimer":4,"SetWaitableTimer":24,
              "CreateWaitableTimerA":12}),[]),
 "FPO":(["trace","ole"],["gap_ole"], same({"OleFlushClipboard":0,"OleIsCurrentClipboard":4,"OleSetClipboard":4,
        "ReleaseStgMedium":4,"CreateBindCtx":8,"OleGetClipboard":4,"PropVariantClear":4}),[]),
 "FPR":(["trace"],["gap_urlmon"], same({"CopyStgMedium":8}),[]),
 "FPS":(["trace","disk_space"],["gap_shell","gap_shell2","gap_shell3"], same({"SHGetFolderPathW":20,
        "SHGetSpecialFolderLocation":12,"SHAppBarMessage":8,"SHBrowseForFolderW":4,"SHGetPathFromIDListW":8,
        "SHGetFolderPathA":20}),[]),
 "FPU":(["trace","user"],["gap_user","gap_user2"], same({"GetMenuItemInfoW":16,"SetMenuItemInfoW":16,
        "InsertMenuItemW":16,"SetMenuInfo":8,"ShowWindowAsync":8,"EnumDisplaySettingsW":12,"FlashWindowEx":4,
        "SendInput":12,"GetKeyboardLayout":4,"UpdateLayeredWindow":36}),[]),
 "FPW":(["trace","winsock_events"],["gap_ws2"], same({"WSASocketW":24,"WSAIoctl":36,"WSAAddressToStringA":20,
        "WSAEventSelect":12,"WSAEnumNetworkEvents":12}),[str(LIB/"libwsock32.a")]),
}

def rebuild(stem):
    baseobjs, gapobjs, apis, exlibs = GAPS[stem]
    # --- def flip ---
    deflines=(COMPAT/f"{stem}.def").read_text().splitlines()
    flipped={}; out=[]
    for ln in deflines:
        m=re.match(r"^(\S+)=fn(\d+)(.*)$",ln)
        if m and m.group(1) in apis:
            exp=m.group(1); flipped[exp]=int(m.group(2)); impl,nb=apis[exp]
            out.append(f"{exp}=_fp_{impl}@{nb}{m.group(3)}")
        else:
            out.append(ln)
    miss=set(apis)-set(flipped)
    assert not miss, f"{stem}: not found in def: {miss}"
    defp=OUT/f"{stem}.def"; defp.write_text("\n".join(out)+"\n")
    # --- asm: drop flipped fn blocks ---
    asm=(COMPAT/f"{stem}.asm").read_text(); remove=set(flipped.values())
    header="bits 32\nsection .text\nextern _FpMissing@8\n"
    blocks=re.findall(r"(global _fn(\d+)\n.*?)(?=global _fn|\Z)", asm, re.S)
    kept=header+"".join(b for b,idx in blocks if int(idx) not in remove)
    asmp=OUT/f"{stem}.asm"; asmp.write_text(kept)
    stubobj=OUT/f"{stem}_stub.obj"
    subprocess.run(["nasm","-f","win32",str(asmp),"-o",str(stubobj)],check=True)
    # --- link ---
    outdll=OUT/f"{stem}.DLL"
    trace = "trace-gdi" if "trace-gdi" in baseobjs else "trace"
    objs=[str(FP10/f"{o}.obj") for o in baseobjs]+[str(stubobj)]+[str(GAPOBJ/f"{g}.obj") for g in gapobjs]
    cmd=["lld-link-19","/nologo","/dll","/subsystem:windows,3.10","/osversion:3.10","/entry:DllMain@12",
         "/nodefaultlib","/safeseh:no","/machine:x86",f"/def:{defp}",f"/out:{outdll}"]+objs+exlibs+SYSLIBS
    r=subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode!=0: print(f"  LINK FAIL {stem}:\n{r.stderr[:900]}"); return (stem,-1)
    # forwarder fixup
    q=pefile.PE(str(outdll))
    for e in q.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.forwarder and e.forwarder.startswith(b'_'): q.set_bytes_at_rva(e.address,e.forwarder[1:]+b'\0')
    q.OPTIONAL_HEADER.CheckSum=q.generate_checksum(); q.write(str(outdll))
    pad_image(str(outdll))
    # count remaining abort stubs
    p2=pefile.PE(str(outdll),fast_load=True)
    p2.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    STUB5=bytes.fromhex('8b04245068')
    def isstub(e):
        if e.forwarder:return False
        try:c=p2.get_data(e.address,15)
        except:return False
        return c[:5]==STUB5 and c[9:10]==b'\xe8' and c[14:15]==b'\xcc'
    rem=sum(1 for e in p2.DIRECTORY_ENTRY_EXPORT.symbols if isstub(e))
    print(f"  {stem:6s} closed {len(apis)} -> remaining stubs {rem}")
    return (stem,rem)

if __name__=="__main__":
    tot=0
    for stem in GAPS:
        s,rem=rebuild(stem)
        if rem>=0: tot+=rem
    print(f"FP10 remaining abort stubs after full closure: {tot}")
