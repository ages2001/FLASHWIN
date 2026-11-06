#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* FP10's comdlg32 shim (FP10.EXE's import is renamed to FPCD.DLL so this unique
   name is always loaded in place of the resident system comdlg32). Pass the
   OPENFILENAME as the engine built it; only retry with the classic 76-byte size
   when a legacy comdlg32 reports CDERR_STRUCTSIZE, so modern comdlg32 keeps the
   Explorer-style dialog. No CRT. */
static FARPROC realcd(const char*n){
    static HMODULE h;
    if(!h){ h=GetModuleHandleA("comdlg32.dll"); if(!h)h=LoadLibraryA("comdlg32.dll"); }
    return h?GetProcAddress(h,n):0;
}
#define CLASSIC_OFN 76u
#define CDERR_STRUCTSIZE_ 0x0001u

/* Browse returns the drive letter as comdlg32 stores it (often "c:\..."). Make
   it upper-case ("C:\...") so paths look like the rest of the system. lpstrFile
   is at offset 28 of OPENFILENAME (same for A and W). */
static void upper_drive(void*ofn,int wide){
    void*f=*(void**)((BYTE*)ofn+28);
    if(!f)return;
    if(wide){WCHAR*w=(WCHAR*)f;if(w[0]>=L'a'&&w[0]<=L'z'&&w[1]==L':')w[0]=(WCHAR)(w[0]-L'a'+L'A');}
    else{char*a=(char*)f;if(a[0]>='a'&&a[0]<='z'&&a[1]==':')a[0]=(char)(a[0]-'a'+'A');}
}
static BOOL ofn_call(const char*name,void*ofn,int wide){
    BOOL(WINAPI*real)(void*); DWORD(WINAPI*cde)(void); DWORD*p=(DWORD*)ofn; DWORD saved; BOOL r;
    real=(BOOL(WINAPI*)(void*))realcd(name);
    if(!real||!ofn) return FALSE;
    saved=p[0];
    r=real(ofn);
    if(!r && saved>CLASSIC_OFN){
        cde=(DWORD(WINAPI*)(void))realcd("CommDlgExtendedError");
        if(cde && cde()==CDERR_STRUCTSIZE_){ p[0]=CLASSIC_OFN; r=real(ofn); p[0]=saved; }
    }
    if(r)upper_drive(ofn,wide);
    return r;
}
BOOL WINAPI fpcd_GetOpenFileNameW(void*o){ return ofn_call("GetOpenFileNameW",o,1); }
BOOL WINAPI fpcd_GetOpenFileNameA(void*o){ return ofn_call("GetOpenFileNameA",o,0); }
BOOL WINAPI fpcd_GetSaveFileNameW(void*o){ return ofn_call("GetSaveFileNameW",o,1); }
BOOL WINAPI fpcd_GetSaveFileNameA(void*o){ return ofn_call("GetSaveFileNameA",o,0); }
DWORD WINAPI fpcd_CommDlgExtendedError(void){ DWORD(WINAPI*real)(void)=(DWORD(WINAPI*)(void))realcd("CommDlgExtendedError"); return real?real():0; }
BOOL WINAPI fpcd_PrintDlgW(void*pd){ BOOL(WINAPI*real)(void*)=(BOOL(WINAPI*)(void*))realcd("PrintDlgW"); return real?real(pd):FALSE; }
