#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
/* shell32 gap closures that are safe on NT 3.1 (whose shell32 lacks the folder
   APIs). These resolve known folders from environment variables and the Windows
   directory only — no shell32 calls — so the proxy loads on NT 3.1. The PIDL
   forms (SHGetPathFromIDListW / SHGetFolderLocation) genuinely need shell32 and
   remain diagnostic stubs. No CRT. */

static void a2w(const char*a,WCHAR*w,int cap){int i=0;while(a[i]&&i<cap-1){w[i]=(unsigned char)a[i];i++;}w[i]=0;}
static void w2a(const WCHAR*w,char*a,int cap){int i=0;while(w[i]&&i<cap-1){a[i]=(char)w[i];i++;}a[i]=0;}
static void appendA(char*dst,const char*add,int cap){int n=0;while(dst[n])n++;while(*add&&n<cap-1)dst[n++]=*add++;dst[n]=0;}

/* Build an ANSI path for a CSIDL from the environment / Windows dir. */
static BOOL folderA(int csidl,char*out,int cap){
    DWORD n; out[0]=0;
    switch(csidl&0xff){
        case CSIDL_WINDOWS:
            n=GetWindowsDirectoryA(out,cap); return n>0&&n<(DWORD)cap;
        case CSIDL_SYSTEM:
            n=GetSystemDirectoryA(out,cap); return n>0&&n<(DWORD)cap;
        case CSIDL_APPDATA:
            n=GetEnvironmentVariableA("USERPROFILE",out,cap);
            if(n&&n<(DWORD)cap){appendA(out,"\\Application Data",cap);return TRUE;} break;
        case CSIDL_LOCAL_APPDATA:
            n=GetEnvironmentVariableA("USERPROFILE",out,cap);
            if(n&&n<(DWORD)cap){appendA(out,"\\Local Settings\\Application Data",cap);return TRUE;} break;
        case CSIDL_PERSONAL:
            n=GetEnvironmentVariableA("USERPROFILE",out,cap);
            if(n&&n<(DWORD)cap){appendA(out,"\\Personal",cap);return TRUE;} break;
        case CSIDL_PROFILE:
            n=GetEnvironmentVariableA("USERPROFILE",out,cap); return n>0&&n<(DWORD)cap;
        default: break;
    }
    /* last resort: USERPROFILE, else TEMP */
    n=GetEnvironmentVariableA("USERPROFILE",out,cap);
    if(n&&n<(DWORD)cap)return TRUE;
    n=GetEnvironmentVariableA("TEMP",out,cap);
    return n>0&&n<(DWORD)cap;
}

HRESULT WINAPI fp_SHGetFolderPathA(HWND hwnd,int csidl,HANDLE token,DWORD flags,LPSTR out){
    (void)hwnd;(void)token;(void)flags; if(!out)return E_INVALIDARG;
    return folderA(csidl,out,MAX_PATH)?S_OK:E_FAIL;
}
HRESULT WINAPI fp_SHGetFolderPathW(HWND hwnd,int csidl,HANDLE token,DWORD flags,LPWSTR out){
    char a[MAX_PATH]; BOOL ok; (void)hwnd;(void)token;(void)flags;
    if(!out)return E_INVALIDARG;
    ok=folderA(csidl,a,MAX_PATH); a2w(a,out,MAX_PATH); return ok?S_OK:E_FAIL;
}
BOOL WINAPI fp_SHGetSpecialFolderPathW(HWND hwnd,LPWSTR path,int csidl,BOOL create){
    char a[MAX_PATH]; (void)hwnd;(void)create; if(!path)return FALSE;
    if(!folderA(csidl,a,MAX_PATH))return FALSE; a2w(a,path,MAX_PATH); return TRUE;
}
/* No app bars on early NT. */
UINT_PTR WINAPI fp_SHAppBarMessage(DWORD msg,void*abd){(void)msg;(void)abd;return 0;}
/* Report cleared shell settings. */
void WINAPI fp_SHGetSettings(LPSHELLFLAGSTATE sfs,DWORD mask){
    (void)mask; if(sfs){unsigned i;for(i=0;i<sizeof(SHELLFLAGSTATE);i++)((BYTE*)sfs)[i]=0;}
}
/* No folder browser dialog; behave as "cancelled". */
LPITEMIDLIST WINAPI fp_SHBrowseForFolderW(void*bi){(void)bi;return 0;}
