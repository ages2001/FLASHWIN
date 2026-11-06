/* Shared CPU/OS policy. Compile this header with the 386 instruction baseline. */
#ifndef FP_CPU_FEATURES_H
#define FP_CPU_FEATURES_H
#include <windows.h>
#include "../winsimd-1.2/include/guard_query.h"
static DWORD fp_cpu_edx(void) {
    DWORD before,after,a,b,c,d;
    __asm__ volatile("pushfl; popl %0":"=r"(before));
    after=before^(1u<<21);
    __asm__ volatile("pushl %0; popfl"::"r"(after):"cc","memory");
    __asm__ volatile("pushfl; popl %0":"=r"(after));
    __asm__ volatile("pushl %0; popfl"::"r"(before):"cc","memory");
    if(!((before^after)&(1u<<21)))return 0;
    a=0;c=0;
    __asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(c),"=d"(d));
    if(a<1)return 0;
    a=1;c=0;
    __asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(c),"=d"(d));
    return d;
}
/* Validate the FpXmmGuard query report WITHOUT a kernel-timestamp whitelist, so
   the guard is accepted on any kernel it supports (NT 3.1/3.5x/4.0, ...), not
   only two hard-coded NT 3.1 builds. The magic/abi/flags still prove a genuine
   XMM+MXCSR-preserving guard is reporting. */
static BOOL fp_guard_status_valid(const FpGuardStatus *s,DWORD bytes) {
    return bytes==sizeof(*s) && s->size==sizeof(*s) && s->magic==FP_GUARD_MAGIC &&
        s->abi==FP_GUARD_ABI && s->flags==FP_GUARD_XMM_MXCSR &&
        s->processors>=1 && s->processors<=64 && s->thread_capacity>=1;
}
/* WINSIMD 1.2 and later preserve XMM0-7/MXCSR themselves (generic #NM context
   guard) where the OS does not. NT: the driver publishes its live status in
   the VOLATILE key ...\Services\winsimd\Runtime (gone after a reboot without
   the driver). 9x: WINSIMD.VXD answers DeviceIoControl code 2 with the same
   status block. Required flags: ACTIVE (1) + XMM (2). */
#define FP_WSGD_MAGIC 0x44475357u
typedef struct { DWORD size, magic, version, flags, comp_lo, comp_hi, cpus, slots, reason; } FpWinsimdGuard;
static BOOL fp_winsimd_guard(void) {
    typedef LONG (WINAPI *OpenFn)(HKEY,LPCSTR,DWORD,REGSAM,PHKEY);
    typedef LONG (WINAPI *QueryFn)(HKEY,LPCSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
    typedef LONG (WINAPI *CloseFn)(HKEY);
    DWORD version=GetVersion(),bytes=0;
    if(version&0x80000000u){
        DWORD buf[16];HANDLE d=CreateFileA("\\\\.\\WINSIMD",0,0,0,OPEN_EXISTING,0,0);
        FpWinsimdGuard*g=(FpWinsimdGuard*)buf;BOOL ok;
        if(d==INVALID_HANDLE_VALUE)return FALSE;
        ok=DeviceIoControl(d,2,0,0,buf,sizeof(buf),&bytes,0);CloseHandle(d);
        return ok&&bytes>=sizeof(*g)&&g->magic==FP_WSGD_MAGIC&&g->version>=0x0102&&(g->flags&3)==3;
    }else{
        HMODULE a=LoadLibraryA("ADVAPI32.DLL");OpenFn op;QueryFn q;CloseFn cl;HKEY k;DWORD type=0,v=0,n=sizeof(v);BOOL ok=FALSE;
        if(!a)return FALSE;
        op=(OpenFn)GetProcAddress(a,"RegOpenKeyExA");q=(QueryFn)GetProcAddress(a,"RegQueryValueExA");cl=(CloseFn)GetProcAddress(a,"RegCloseKey");
        if(op&&q&&cl&&op(HKEY_LOCAL_MACHINE,"SYSTEM\\CurrentControlSet\\Services\\winsimd\\Runtime",0,KEY_READ,&k)==ERROR_SUCCESS){
            if(q(k,"GuardFlags",0,&type,(LPBYTE)&v,&n)==ERROR_SUCCESS&&type==REG_DWORD&&(v&3)==3)ok=TRUE;
            cl(k);}
        return ok;
    }
}
static BOOL fp_xmm_context_ready(void) {
    typedef BOOL (WINAPI *FeatureFn)(DWORD);
    DWORD version=GetVersion(),bytes=0;
    HANDLE device;
    FpGuardStatus s={0};
    /* Native NT5+ (2000/XP): the OS preserves XMM itself; trust its own report.
       No WINSIMD driver is needed on these kernels. */
    if(!(version&0x80000000u) && (version&255)>=5) {
        FeatureFn feature=(FeatureFn)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),
                                                   "IsProcessorFeaturePresent");
        return feature && feature(PF_XMMI_INSTRUCTIONS_AVAILABLE);
    }
    /* Windows 98 SE (4.10.2222) and ME save XMM state themselves (FXSAVE);
       Windows 95 and Windows 98 first edition (4.10.1998) do not. */
    if(version&0x80000000u){
        typedef BOOL (WINAPI *VerFn)(LPOSVERSIONINFOA);OSVERSIONINFOA v;
        VerFn gv=(VerFn)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"GetVersionExA");
        v.dwOSVersionInfoSize=sizeof(v);
        if(gv&&gv(&v)&&(v.dwMajorVersion>4||(v.dwMajorVersion==4&&(v.dwMinorVersion>10||
           (v.dwMinorVersion==10&&(v.dwBuildNumber&0xFFFF)>=2222)))))
            return (fp_cpu_edx()&(1u<<26))!=0;
    }
    /* Legacy NT (3.1/3.5x/4.0), Windows 95 and 98 first edition do not preserve
       XMM registers across thread switches. A driver that merely ENABLES SSE
       (CR4.OSFXSR, e.g. WINSIMD 1.1) is NOT enough: measured on NT 3.1 with it,
       about 16% of XMM register checks were corrupted by other threads
       (XMMSTRS.EXE), which crashed the players' memory manager.
       Accept WINSIMD 1.2+ (generic context guard) or the older exact-kernel
       NT 3.1 research adapter (\\.\FpXmmGuard). */
    if(fp_winsimd_guard())return TRUE;
    device=CreateFileA("\\\\.\\FpXmmGuard",0,FILE_SHARE_READ|FILE_SHARE_WRITE,
                       0,OPEN_EXISTING,0,0);
    if(device==INVALID_HANDLE_VALUE)return FALSE;
    if(!DeviceIoControl(device,FP_GUARD_IOCTL,0,0,&s,sizeof(s),&bytes,0))bytes=0;
    CloseHandle(device);
    return fp_guard_status_valid(&s,bytes);
}
static BOOL fp_sse_feature(DWORD feature) {
    DWORD flag=feature==PF_XMMI_INSTRUCTIONS_AVAILABLE ? (1u<<25) : (1u<<26);
    return (fp_cpu_edx()&flag)!=0 && fp_xmm_context_ready();
}
#endif
