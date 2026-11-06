#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* kernel32 gap closures for early NT. No CRT, no SIMD. */

/* FindFirstFileExA: early NT has only FindFirstFileA. For the standard info
   level and name-match search (what the Flash file enumerator uses) the result
   record is identical, so forward. */
HANDLE WINAPI fp_FindFirstFileExA(LPCSTR name,int infoLevel,LPVOID findData,
                                  int searchOp,LPVOID filter,DWORD flags){
    (void)infoLevel;(void)searchOp;(void)filter;(void)flags;
    return FindFirstFileA(name,(LPWIN32_FIND_DATAA)findData);
}

/* GetModuleHandleExW: compose from the classic queries. */
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_PIN
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#endif
BOOL WINAPI fp_GetModuleHandleExW(DWORD flags,LPCWSTR name,HMODULE*mod){
    HMODULE h;
    if(!mod)return FALSE; *mod=0;
    if(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS){
        MEMORY_BASIC_INFORMATION mbi;
        if(!VirtualQuery((LPCVOID)name,&mbi,sizeof(mbi))){SetLastError(ERROR_MOD_NOT_FOUND);return FALSE;}
        h=(HMODULE)mbi.AllocationBase;
    } else {
        h=GetModuleHandleW(name);
    }
    if(!h){SetLastError(ERROR_MOD_NOT_FOUND);return FALSE;}
    if(!(flags&GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT) && name){
        /* keep a reference like the real API (not for FROM_ADDRESS) */
        WCHAR path[MAX_PATH];
        if(!(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) &&
           GetModuleFileNameW(h,path,MAX_PATH)) LoadLibraryW(path);
    }
    *mod=h; return TRUE;
}

/* HeapWalk is a diagnostic; early NT has no equivalent. Report "no entries". */
BOOL WINAPI fp_HeapWalk(HANDLE heap,LPPROCESS_HEAP_ENTRY entry){
    (void)heap;(void)entry; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}

/* EnumSystemLocalesW: forward to the ANSI enumerator when the running kernel
   exports it (loaded dynamically so a kernel without it does not break loading);
   otherwise enumerate just the system default locale. Locale ids are ASCII hex. */
typedef BOOL (WINAPI *ENUMLOCA)(LOCALE_ENUMPROCA,DWORD);
static LOCALE_ENUMPROCW g_wproc;
static BOOL CALLBACK fp_locale_thunk(LPSTR a){
    WCHAR w[16]; int i=0;
    while(a[i]&&i<15){w[i]=(unsigned char)a[i];i++;} w[i]=0;
    return g_wproc(w);
}
BOOL WINAPI fp_EnumSystemLocalesW(LOCALE_ENUMPROCW proc,DWORD flags){
    HMODULE k; ENUMLOCA ea;
    if(!proc)return FALSE; g_wproc=proc;
    k=GetModuleHandleA("kernel32.dll");
    ea=k?(ENUMLOCA)GetProcAddress(k,"EnumSystemLocalesA"):0;
    if(ea) return ea((LOCALE_ENUMPROCA)fp_locale_thunk,flags);
    { WCHAR w[16]; LCID l=GetSystemDefaultLCID(); const char*h="0123456789abcdef"; int i;
      for(i=0;i<8;i++) w[i]=h[(l>>(28-4*i))&0xf]; w[8]=0; return proc(w); }
}
