#!/usr/bin/env python3
"""Rebuild FP32 proxies closing ALL gaps except QueueUserAPC's functionality
(it is present but non-functional). Each proxy keeps its original F32* LIBRARY
name (no engine rename). clang-19 objects + lld-link-19 + pad_image. Run -I.
"""
import sys, re, subprocess, pefile
sys.path.insert(0,"/home/claude/fp32build")
sys.path.insert(0,"/home/claude/fp_main/FP10-FP32-WINSIMD-NTCPUPAE-20261102-ALL-FILES/SRC/PLAYER/analysis")
import buildall
from pad_legacy_pe import pad_image
B=buildall.B; OUT=buildall.OUT; SYSLIBS=buildall.SYSLIBS; PKG=buildall.PKG
LIB=buildall.LIB
OUTDIR=OUT/"gaps"; OUTDIR.mkdir(exist_ok=True)

# proxy -> (extra_gap_objs, {export: argbytes OR (impl,argbytes)}, extra_libs)
GAPS={
 "F32G":(["gap_gdi"],{"GetICMProfileA":12,"EnumFontFamiliesExW":20},[]),
 "F32S":(["gap_shell","gap_shell2"],{"SHGetFolderPathW":20,"SHGetFolderPathA":20,"SHGetSpecialFolderPathW":16,
          "SHAppBarMessage":8,"SHGetSettings":8,"SHBrowseForFolderW":4,"SHGetFolderLocation":20,"SHGetPathFromIDListW":8},[]),
 "F32U":(["gap_user","gap_user2"],{"ShowWindowAsync":8,"FlashWindowEx":4,"RegisterClassExA":4,"GetKeyboardLayout":4,
          "EnumDisplaySettingsW":12,"GetUserObjectInformationW":20,"GetWindowInfo":8,"UpdateLayeredWindow":36,
          "InsertMenuItemW":16,"GetMenuItemInfoW":16,"SetMenuItemInfoW":16,"SetMenuInfo":8,"SendInput":12},[]),
 "F32K":(["gap_kernel","gap_kernel2","gap_fiber"],{"FindFirstFileExA":24,"GetModuleHandleExW":12,"HeapWalk":8,"EnumSystemLocalesW":8,
          "CreateWaitableTimerA":12,"CreateWaitableTimerW":12,"SetWaitableTimer":24,"CancelWaitableTimer":4,
          "CreateTimerQueueTimer":28,"DeleteTimerQueueTimer":12,"ConvertThreadToFiber":4,"ConvertFiberToThread":0,
          "CreateFiber":12,"DeleteFiber":4,"SwitchToFiber":4,"QueueUserAPC":12},[]),
 "F32R":(["gap_urlmon"],{"CopyStgMedium":8},[]),
 "F32O":(["gap_ole"],{"OleSetClipboard":4,"CoSetProxyBlanket":32,"PropVariantClear":4,"OleGetClipboard":4,
          "OleFlushClipboard":0,"OleIsCurrentClipboard":4,"ReleaseStgMedium":4,"CoInitializeEx":8,
          "MkParseDisplayName":16,"CreateBindCtx":8},[]),
 "F32CM":(["gap_mscms"],{"CloseColorProfile":4,"OpenColorProfileW":16,"DeleteColorTransform":4,
          "TranslateBitmapBits":44,"CreateColorTransformW":16},[]),
 "F32I":(["gap_wininet"],{"HttpOpenRequestA":32,"HttpSendRequestA":20,"HttpQueryInfoA":20,"InternetReadFile":16,
          "InternetConnectA":32,"InternetCloseHandle":4,"InternetOpenA":20},[]),
 "F32C":(["gap_crypt"],{"CertDuplicateCertificateContext":4,"CertGetCertificateContextProperty":16,
          "CertFindCertificateInStore":24,"CertVerifySubjectCertificateContext":12,"CertFreeCertificateContext":4,
          "CertCompareCertificate":12,"CryptVerifyMessageSignature":28,"CryptGetMessageCertificates":20,
          "CertOpenStore":20,"CertEnumCertificatesInStore":8,"CertCloseStore":8,"CertCreateCertificateContext":12},[]),
 "F32W":(["gap_ws2"],{"WSAEnumNetworkEvents":12,"WSASocketW":24,"WSAIoctl":36,"WSAAddressToStringA":20,
          "getaddrinfo":16,"freeaddrinfo":4,"getnameinfo":28,"WSASocketA":24,"WSAEventSelect":12},[str(LIB/"libwsock32.a")]),
 "F32D":(["gap_dsound"],{"ordinal1":("dsOrdinal1",12),"ordinal8":("dsOrdinal8",8)},[]),
}

def rebuild(stem):
    gapobjs, apis, exlibs = GAPS[stem]
    new,trace,baseobjs,baseextra = buildall.PROXIES[stem]
    deflines=(B/f"{stem}.def").read_text().splitlines()
    flipped={}; out_def=[]
    for ln in deflines:
        m=re.match(r"^(\S+)=missing(\d+)(.*)$",ln)
        if m and m.group(1) in apis:
            exp=m.group(1); flipped[exp]=int(m.group(2)); v=apis[exp]
            impl,nb = (v if isinstance(v,tuple) else (exp,v))
            out_def.append(f"{exp}=_fp_{impl}@{nb}{m.group(3)}")
        else:
            out_def.append(ln)
    missing_api=set(apis)-set(flipped)
    assert not missing_api, f"{stem}: not found in def: {missing_api}"
    defp=OUTDIR/f"{stem}.def"; defp.write_text("\n".join(out_def)+"\n")
    # asm: keep only non-flipped stub blocks
    asm=(B/f"{stem}.asm").read_text(); remove=set(flipped.values())
    header="bits 32\nsection .text\nextern _FpMissing@8\n"
    blocks=re.findall(r"(global _missing(\d+)\n.*?)(?=global _missing|\Z)", asm, re.S)
    kept=header+"".join(blk for blk,idx in blocks if int(idx) not in remove)
    asmp=OUTDIR/f"{stem}.asm"; asmp.write_text(kept)
    stubobj=OUTDIR/f"{stem}_stub.obj"
    subprocess.run(["nasm","-f","win32",str(asmp),"-o",str(stubobj)],check=True)
    outdll=OUTDIR/f"{stem}.DLL"
    cmd=["lld-link-19","/nologo","/dll","/subsystem:windows,3.10","/osversion:3.10","/entry:DllMain@12",
         "/nodefaultlib","/safeseh:no","/machine:x86",f"/def:{defp}",f"/out:{outdll}",
         str(OUT/f"{trace}.obj"),str(stubobj)]+[str(OUT/f"{o}.obj") for o in baseobjs]+\
        [str(OUT/f"{g}.obj") for g in gapobjs]+baseextra+exlibs+SYSLIBS
    r=subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode!=0: print(f"  LINK FAIL {stem}:\n{r.stderr[:700]}"); return (stem,-1)
    q=pefile.PE(str(outdll))
    for e in q.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.forwarder and e.forwarder.startswith(b'_'): q.set_bytes_at_rva(e.address,e.forwarder[1:]+b'\0')
    q.OPTIONAL_HEADER.CheckSum=q.generate_checksum(); q.write(str(outdll))
    pad_image(str(outdll))
    # count remaining stubs
    p2=pefile.PE(str(outdll),fast_load=True);p2.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    def isstub(e):
        if e.forwarder:return False
        try:c=p2.get_data(e.address,15)
        except:return False
        return c[:5]==bytes.fromhex('8b04245068') and c[9:10]==b'\xe8' and c[14:15]==b'\xcc'
    rem=sum(1 for e in p2.DIRECTORY_ENTRY_EXPORT.symbols if isstub(e))
    print(f"  {stem:6s} closed {len(apis)} -> remaining stubs {rem}")
    return (stem,rem)

if __name__=="__main__":
    tot=0
    for stem in GAPS:
        s,rem=rebuild(stem)
        if rem>=0: tot+=rem
    print(f"FP32 remaining abort stubs after full closure: {tot}")
