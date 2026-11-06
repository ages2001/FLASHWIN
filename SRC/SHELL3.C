#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
/* FP10 shell32 extra: SHGetSpecialFolderLocation. Forward to shell32 when the
   classic entry is present (it shipped on NT 3.1); fail cleanly otherwise. No CRT. */
HRESULT WINAPI fp_SHGetSpecialFolderLocation(HWND hwnd,int csidl,LPITEMIDLIST*ppidl){
    HRESULT (WINAPI *f)(HWND,int,LPITEMIDLIST*);HRESULT r;
    HMODULE h=GetModuleHandleA("shell32.dll"); if(!h)h=LoadLibraryA("shell32.dll");
    if(ppidl)*ppidl=0;
    f=(HRESULT(WINAPI*)(HWND,int,LPITEMIDLIST*))(h?GetProcAddress(h,"SHGetSpecialFolderLocation"):0);
    if(!f) return E_FAIL;
    r=f(hwnd,csidl,ppidl);return r;
}
