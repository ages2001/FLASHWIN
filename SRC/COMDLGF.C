#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* NT 3.1 / early comdlg32 rejects the newer (88-byte) OPENFILENAME with
   CDERR_STRUCTSIZE, so the player's Browse button silently fails there. Modern
   comdlg32 (NT4/2000/XP and Win9x) accepts the full struct and uses the
   Explorer-style dialog the engine asked for - clamping lStructSize there would
   force the legacy dialog and break the result the engine expects. So the struct
   is passed AS THE ENGINE BUILT IT, and only retried with the classic 76-byte
   size when the real comdlg32 reports CDERR_STRUCTSIZE. The real comdlg32 is
   loaded dynamically (this proxy has a different name, no recursion). No CRT. */
static FARPROC cdproc(const char*n){
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
static BOOL call_fixed(const char*name,void*ofn,int wide){
    BOOL(WINAPI*real)(void*); DWORD(WINAPI*cde)(void); DWORD*p=(DWORD*)ofn; DWORD saved; BOOL r;
    real=(BOOL(WINAPI*)(void*))cdproc(name);
    if(!real||!ofn) return FALSE;
    saved=p[0];
    r=real(ofn);                                   /* try as the engine built it */
    if(!r && saved>CLASSIC_OFN){                    /* maybe a legacy comdlg32 */
        cde=(DWORD(WINAPI*)(void))cdproc("CommDlgExtendedError");
        if(cde && cde()==CDERR_STRUCTSIZE_){ p[0]=CLASSIC_OFN; r=real(ofn); p[0]=saved; }
    }
    if(r)upper_drive(ofn,wide);
    return r;
}
BOOL WINAPI fp_GetOpenFileNameW(void*ofn){ return call_fixed("GetOpenFileNameW",ofn,1); }
BOOL WINAPI fp_GetSaveFileNameW(void*ofn){ return call_fixed("GetSaveFileNameW",ofn,1); }
BOOL WINAPI fp_GetOpenFileNameA(void*ofn){ return call_fixed("GetOpenFileNameA",ofn,0); }
BOOL WINAPI fp_GetSaveFileNameA(void*ofn){ return call_fixed("GetSaveFileNameA",ofn,0); }
