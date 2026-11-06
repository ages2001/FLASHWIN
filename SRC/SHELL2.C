#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
/* shell32 PIDL gap closures. These need the shell32 folder/PIDL APIs, which
   exist on NT 3.51+ but not NT 3.1. Resolve them dynamically so the proxy still
   loads where they are absent, and fail cleanly there. No CRT. */

typedef HRESULT (WINAPI *PFNSFL)(HWND,int,LPITEMIDLIST*);
typedef BOOL    (WINAPI *PFNPIA)(LPCITEMIDLIST,LPSTR);

static PFNSFL get_sfl(void){
    HMODULE h=GetModuleHandleA("shell32.dll"); if(!h)h=LoadLibraryA("shell32.dll");
    return h?(PFNSFL)GetProcAddress(h,"SHGetSpecialFolderLocation"):0;
}
static PFNPIA get_pia(void){
    HMODULE h=GetModuleHandleA("shell32.dll"); if(!h)h=LoadLibraryA("shell32.dll");
    return h?(PFNPIA)GetProcAddress(h,"SHGetPathFromIDListA"):0;
}

HRESULT WINAPI fp_SHGetFolderLocation(HWND hwnd,int csidl,HANDLE token,DWORD flags,LPITEMIDLIST*ppidl){
    PFNSFL f=get_sfl(); (void)token;(void)flags;
    if(!ppidl) return E_INVALIDARG; *ppidl=0;
    if(!f){ SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return E_FAIL; }
    return f(hwnd,csidl&0xff,ppidl);
}
BOOL WINAPI fp_SHGetPathFromIDListW(LPCITEMIDLIST pidl,LPWSTR path){
    PFNPIA f=get_pia(); char a[MAX_PATH]; int i;
    if(!path) return FALSE;
    if(!f){ path[0]=0; return FALSE; }
    if(!f(pidl,a)){ path[0]=0; return FALSE; }
    for(i=0;a[i]&&i<MAX_PATH-1;i++) path[i]=(unsigned char)a[i];
    path[i]=0; return TRUE;
}
