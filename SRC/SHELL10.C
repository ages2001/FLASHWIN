#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
/* FP10 SHGetFolderPath bridge. A native SHGetFolderPathW (shell32 on
   2000/ME/XP, SHFOLDER.DLL on older systems that have it) is always used
   first. Without one, per-user application data (Flash SharedObjects and
   settings) goes to application-owned storage next to the player:
     <player folder>\UserData\<8-character pieces of the user SID>  (NT)
     <player folder>\UserData\Default                               (9x)
   FP10_USERDATA_ROOT may name another base folder (used by the portable
   launcher so saves live next to the portable EXE). The chosen folder is
   published in FP10_SAVEROOT; on volumes limited to 8.3 names it is also
   published in FP10_USERDATA, which enables the name adapter in FPK.DLL.
   Other folders keep the previous environment-based answers. No CRT. */
static void a2w(const char*a,WCHAR*w,int cap){int i=0;while(a[i]&&i<cap-1){w[i]=(unsigned char)a[i];i++;}w[i]=0;}
static int lenA(const char*s){int n=0;while(s[n])n++;return n;}
static BOOL appendA(char*dst,const char*add,int cap){int n=lenA(dst),k=lenA(add);if(n+k>=cap)return FALSE;while(*add)dst[n++]=*add++;dst[n]=0;return TRUE;}

typedef HRESULT (WINAPI *NativeW)(HWND,int,HANDLE,DWORD,LPWSTR);
typedef HRESULT (WINAPI *NativeA)(HWND,int,HANDLE,DWORD,LPSTR);
static FARPROC native_proc(const char*name){
    static const char*dlls[2]={"SHELL32.DLL","SHFOLDER.DLL"};int i;
    for(i=0;i<2;i++){HMODULE h=GetModuleHandleA(dlls[i]);FARPROC f;if(!h)h=LoadLibraryA(dlls[i]);if(!h)continue;f=GetProcAddress(h,name);if(f)return f;}
    return 0;
}
static BOOL directoryA(const char*p){DWORD a=GetFileAttributesA(p);if(a!=0xFFFFFFFF)return (a&FILE_ATTRIBUTE_DIRECTORY)!=0;if(CreateDirectoryA(p,0))return TRUE;a=GetFileAttributesA(p);return a!=0xFFFFFFFF&&(a&FILE_ATTRIBUTE_DIRECTORY);}
static BOOL treeA(const char*path){char part[MAX_PATH];int i,n=lenA(path);if(n<3||n>=MAX_PATH||path[1]!=':'||path[2]!='\\')return FALSE;
    for(i=0;i<=n;i++)part[i]=path[i];
    for(i=3;i<n;i++)if(part[i]=='\\'){part[i]=0;if(!directoryA(part))return FALSE;part[i]='\\';}
    return directoryA(part);}
/* Append \XXXXXXXX pieces of the current user's SID (lossless, 8.3-safe). */
static BOOL user_key(char*path){
    typedef BOOL(WINAPI *OpenToken)(HANDLE,DWORD,PHANDLE);
    typedef BOOL(WINAPI *TokenInfo)(HANDLE,TOKEN_INFORMATION_CLASS,LPVOID,DWORD,PDWORD);
    typedef DWORD(WINAPI *SidLength)(PSID);
    DWORD raw[128],needed,n,i,k;BYTE*sid;HANDLE token;HMODULE adv;OpenToken ot;TokenInfo ti;SidLength sl;char chunk[10];
    if(GetVersion()&0x80000000u)return appendA(path,"\\Default",MAX_PATH);
    adv=LoadLibraryA("ADVAPI32.DLL");if(!adv)return FALSE;
    ot=(OpenToken)GetProcAddress(adv,"OpenProcessToken");ti=(TokenInfo)GetProcAddress(adv,"GetTokenInformation");sl=(SidLength)GetProcAddress(adv,"GetLengthSid");
    if(!ot||!ti||!sl||!ot(GetCurrentProcess(),TOKEN_QUERY,&token))return FALSE;
    if(!ti(token,TokenUser,raw,sizeof(raw),&needed)){CloseHandle(token);return FALSE;}
    CloseHandle(token);sid=(BYTE*)((TOKEN_USER*)raw)->User.Sid;n=sl(sid);
    if(n<8||n>68||sid<(BYTE*)raw||sid+n>(BYTE*)raw+sizeof(raw))return FALSE;
    for(i=0;i<n;){chunk[0]='\\';k=1;while(i<n&&k<9){BYTE v=sid[i++];chunk[k++]="0123456789ABCDEF"[v>>4];chunk[k++]="0123456789ABCDEF"[v&15];}chunk[k]=0;if(!appendA(path,chunk,MAX_PATH))return FALSE;}
    return TRUE;
}
/* Resolve (and create) the private per-user folder once per process. */
static BOOL private_appdata(char*out){
    static char cached[MAX_PATH];static int state;char path[MAX_PATH],volume[4];DWORD n,maxlen=255,fsflags;int i,slash=-1;
    if(state==1){for(i=0;(out[i]=cached[i])!=0;i++){}return TRUE;}
    n=GetEnvironmentVariableA("FP10_USERDATA_ROOT",path,MAX_PATH-40);
    if(!n||n>=MAX_PATH-40){n=GetModuleFileNameA(0,path,MAX_PATH-40);if(!n||n>=MAX_PATH-40)return FALSE;
        for(i=0;path[i];i++)if(path[i]=='\\')slash=i;if(slash<2)return FALSE;path[slash]=0;}
    while(lenA(path)>3&&path[lenA(path)-1]=='\\')path[lenA(path)-1]=0;
    if(!appendA(path,"\\UserData",MAX_PATH)||!user_key(path)||!treeA(path))return FALSE;
    SetEnvironmentVariableA("FP10_SAVEROOT",path);
    if(path[1]==':'){volume[0]=path[0];volume[1]=':';volume[2]='\\';volume[3]=0;
        if(!GetVolumeInformationA(volume,0,0,0,&maxlen,&fsflags,0,0))maxlen=255;}
    if(maxlen<255)SetEnvironmentVariableA("FP10_USERDATA",path);
    for(i=0;(cached[i]=path[i])!=0;i++){}state=1;
    for(i=0;(out[i]=path[i])!=0;i++){}return TRUE;
}
static BOOL folderA(int csidl,char*out,int cap){
    DWORD n; out[0]=0;
    switch(csidl&0xff){
        case CSIDL_WINDOWS: n=GetWindowsDirectoryA(out,cap); return n>0&&n<(DWORD)cap;
        case CSIDL_SYSTEM: n=GetSystemDirectoryA(out,cap); return n>0&&n<(DWORD)cap;
        case CSIDL_APPDATA: case CSIDL_LOCAL_APPDATA: return private_appdata(out);
        case CSIDL_PERSONAL:
            n=GetEnvironmentVariableA("USERPROFILE",out,cap);
            if(n&&n<(DWORD)cap){appendA(out,"\\Personal",cap);return TRUE;} break;
        case CSIDL_PROFILE:
            n=GetEnvironmentVariableA("USERPROFILE",out,cap); if(n>0&&n<(DWORD)cap)return TRUE; break;
        default: break;
    }
    n=GetEnvironmentVariableA("USERPROFILE",out,cap);
    if(n&&n<(DWORD)cap)return TRUE;
    n=GetEnvironmentVariableA("TEMP",out,cap);
    return n>0&&n<(DWORD)cap;
}
HRESULT WINAPI fp_SHGetFolderPathA(HWND hwnd,int csidl,HANDLE token,DWORD flags,LPSTR out){
    NativeA f=(NativeA)native_proc("SHGetFolderPathA");
    if(!out)return E_INVALIDARG;
    if(f){HRESULT r=f(hwnd,csidl,token,flags,out);if(r==S_OK)return r;}
    return folderA(csidl,out,MAX_PATH)?S_OK:E_FAIL;
}
HRESULT WINAPI fp_SHGetFolderPathW(HWND hwnd,int csidl,HANDLE token,DWORD flags,LPWSTR out){
    char a[MAX_PATH]; NativeW f=(NativeW)native_proc("SHGetFolderPathW");
    if(!out)return E_INVALIDARG;
    if(f){HRESULT r=f(hwnd,csidl,token,flags,out);if(r==S_OK)return r;}
    if(!folderA(csidl,a,MAX_PATH)){out[0]=0;return E_FAIL;}
    a2w(a,out,MAX_PATH); return S_OK;
}
BOOL WINAPI fp_SHGetSpecialFolderPathW(HWND hwnd,LPWSTR path,int csidl,BOOL create){
    char a[MAX_PATH]; (void)hwnd;(void)create; if(!path)return FALSE;
    if(!folderA(csidl,a,MAX_PATH))return FALSE; a2w(a,path,MAX_PATH); return TRUE;
}
UINT_PTR WINAPI fp_SHAppBarMessage(DWORD msg,void*abd){(void)msg;(void)abd;return 0;}
void WINAPI fp_SHGetSettings(LPSHELLFLAGSTATE sfs,DWORD mask){(void)mask; if(sfs){unsigned i;for(i=0;i<sizeof(SHELLFLAGSTATE);i++)((BYTE*)sfs)[i]=0;}}
LPITEMIDLIST WINAPI fp_SHBrowseForFolderW(void*bi){(void)bi;return 0;}
