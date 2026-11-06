/* FP9X.DLL - Unicode layer for running the Flash Player ports on Windows
   95 / 98 / ME.

   Flash Player 10 and 32 call the Unicode ("W") Win32 API throughout.
   Windows 9x exports most W functions only as stubs that fail with
   ERROR_CALL_NOT_IMPLEMENTED. This DLL implements the W functions the
   players and their compatibility proxies use, on top of the ANSI ("A")
   functions, converting strings with the ANSI code page.

   How it is reached (Windows 9x only; on Windows NT nothing is done):
   * Proxy exports that were forwarders are stubs (see unforward.py) that
     ask FP9X.DLL first when running on 9x.
   * When FP9X.DLL is loaded on 9x it rewrites the import address tables of
     the player EXE and every non-system DLL in the process so that their
     imports of these W functions point here. DLLs loaded later
     (LoadLibrary*, delay-load) are patched as well.

   Window procedures: a W window/dialog procedure is wrapped in a small
   executable thunk that is registered with the ANSI API; the thunk converts
   ANSI messages that carry text (WM_CHAR, WM_SETTEXT, WM_GETTEXT,
   WM_CREATE, ...) to Unicode before calling the W procedure.

   No CRT. Builds with clang (i686-w64-windows-gnu), 386 baseline. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <ddeml.h>

/* ------------------------------------------------------------------ */
/* basic helpers                                                       */
DWORD WINAPI x9_CharUpperBuffW(LPWSTR s,DWORD n);DWORD WINAPI x9_CharLowerBuffW(LPWSTR s,DWORD n);
static void*traced(const char*name,void*fn);static BOOL from_system(void*p);
static HANDLE heap;
static void *mem(DWORD n){return HeapAlloc(heap?heap:(heap=GetProcessHeap()),HEAP_ZERO_MEMORY,n?n:1);}
static void unmem(void *p){if(p)HeapFree(heap?heap:(heap=GetProcessHeap()),0,p);}
static int wlen(LPCWSTR s){int n=0;if(s)while(s[n])n++;return n;}
static int alen(LPCSTR s){int n=0;if(s)while(s[n])n++;return n;}
static BOOL is_res(LPCVOID p){return ((ULONG_PTR)p>>16)==0;}
/* W -> new ANSI string (NULL stays NULL, MAKEINTRESOURCE/atom passes through) */
static LPSTR A(LPCWSTR w){int n;LPSTR a;if(!w||is_res(w))return (LPSTR)w;
 n=WideCharToMultiByte(CP_ACP,0,w,-1,0,0,0,0);a=(LPSTR)mem(n+1);WideCharToMultiByte(CP_ACP,0,w,-1,a,n,0,0);return a;}
static void FA(LPCVOID p,LPCVOID orig){if(p&&p!=orig&&!is_res(p))unmem((void*)p);}
#define FREEA(a,w) FA(a,w)
/* ANSI -> new W string */
static LPWSTR W(LPCSTR a){int n;LPWSTR w;if(!a||is_res(a))return (LPWSTR)a;
 n=MultiByteToWideChar(CP_ACP,0,a,-1,0,0);w=(LPWSTR)mem((n+1)*2);MultiByteToWideChar(CP_ACP,0,a,-1,w,n);return w;}
/* ANSI buffer -> caller W buffer (cap in WCHARs). Returns chars written w/o NUL. */
static int a2wbuf(LPCSTR a,LPWSTR w,int cap){int n;if(!w||cap<=0)return 0;
 n=MultiByteToWideChar(CP_ACP,0,a,-1,w,cap);if(!n){w[cap-1]=0;n=wlen(w);return n;}return n-1;}
static int w2abuf(LPCWSTR w,LPSTR a,int cap){int n;if(!a||cap<=0)return 0;
 n=WideCharToMultiByte(CP_ACP,0,w,-1,a,cap,0,0);if(!n){a[cap-1]=0;return alen(a);}return n-1;}
/* length (WCHARs) an ANSI string becomes */
static int a2wlen(LPCSTR a){return a?MultiByteToWideChar(CP_ACP,0,a,-1,0,0)-1:0;}

/* Optional diagnostics: set FP9X_LOG=<file>, or create an empty FP9X.LOG
   next to FP9X.DLL, to get a trace. */
static HMODULE self;
static HANDLE logf=INVALID_HANDLE_VALUE;static BOOL logchk;
static void lg(const char*fmt,...){char b[600];va_list ap;DWORD n,le=GetLastError();
 if(!logchk){char p[MAX_PATH];logchk=TRUE;if(GetEnvironmentVariableA("FP9X_LOG",p,sizeof(p)))logf=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,0,OPEN_ALWAYS,0,0);
  else{DWORD n=GetModuleFileNameA(self,p,MAX_PATH-9);while(n&&p[n-1]!='\\')n--;lstrcpyA(p+n,"FP9X.LOG");logf=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);}if(logf!=INVALID_HANDLE_VALUE)SetFilePointer(logf,0,0,FILE_END);}
 if(logf==INVALID_HANDLE_VALUE){SetLastError(le);return;}va_start(ap,fmt);n=wvsprintfA(b,fmt,ap);va_end(ap);b[n++]='\r';b[n++]='\n';WriteFile(logf,b,n,&n,0);FlushFileBuffers(logf);SetLastError(le);}
static const char*dbg(LPCWSTR w){static char b[4][128];static int i;char*p=b[i++&3];if(!w)return "(null)";if(is_res(w)){wsprintfA(p,"#%u",(UINT)(ULONG_PTR)w);return p;}w2abuf(w,p,128);return p;}

/* ------------------------------------------------------------------ */
/* kernel32                                                            */

/* Windows 95 has no NTDLL.DLL; FP32 looks up RtlInitUnicodeString there and
   calls it unchecked. Answer for a missing NTDLL with FP9X itself, which
   exports that function (and nothing else of NTDLL). */
static BOOL is_ntdll(LPCSTR a){static const char*t="ntdll";int i;if(!a||is_res(a))return FALSE;for(i=0;t[i];i++)if((a[i]|32)!=t[i])return FALSE;
 return !a[i]||((a[i]=='.')&&(a[i+1]|32)=='d'&&(a[i+2]|32)=='l'&&(a[i+3]|32)=='l'&&!a[i+4]);}
HMODULE WINAPI x9_GetModuleHandleW(LPCWSTR n){LPSTR a=A(n);HMODULE h=GetModuleHandleA(a);if(!h&&is_ntdll(a))h=self;FREEA(a,n);return h;}
DWORD WINAPI x9_GetModuleFileNameW(HMODULE m,LPWSTR out,DWORD cap){char b[MAX_PATH];DWORD n=GetModuleFileNameA(m,b,sizeof(b));if(!n)return 0;b[n<MAX_PATH?n:MAX_PATH-1]=0;return a2wbuf(b,out,cap);}
HMODULE WINAPI x9_LoadLibraryW(LPCWSTR n);
HMODULE WINAPI x9_LoadLibraryExW(LPCWSTR n,HANDLE f,DWORD fl);
static LPWSTR cmdw;
LPWSTR WINAPI x9_GetCommandLineW(void){if(!cmdw)cmdw=W(GetCommandLineA());return cmdw;}
VOID WINAPI x9_GetStartupInfoW(LPSTARTUPINFOW si){STARTUPINFOA a;GetStartupInfoA(&a);
 si->cb=sizeof(*si);si->lpReserved=0;si->lpDesktop=0;si->lpTitle=0;si->dwX=a.dwX;si->dwY=a.dwY;si->dwXSize=a.dwXSize;si->dwYSize=a.dwYSize;
 si->dwXCountChars=a.dwXCountChars;si->dwYCountChars=a.dwYCountChars;si->dwFillAttribute=a.dwFillAttribute;si->dwFlags=a.dwFlags;
 si->wShowWindow=a.wShowWindow;si->cbReserved2=0;si->lpReserved2=0;si->hStdInput=a.hStdInput;si->hStdOutput=a.hStdOutput;si->hStdError=a.hStdError;}
DWORD WINAPI x9_GetEnvironmentVariableW(LPCWSTR n,LPWSTR out,DWORD cap){LPSTR a=A(n);DWORD k=GetEnvironmentVariableA(a,0,0),r;char*b;
 if(!k){FREEA(a,n);return 0;}b=(char*)mem(k+1);GetEnvironmentVariableA(a,b,k+1);FREEA(a,n);r=a2wlen(b);
 if(!out||cap<=r){unmem(b);return r+1;}a2wbuf(b,out,cap);unmem(b);return r;}
BOOL WINAPI x9_SetEnvironmentVariableW(LPCWSTR n,LPCWSTR v){LPSTR a=A(n),b=A(v);BOOL r=SetEnvironmentVariableA(a,b);FREEA(a,n);FREEA(b,v);return r;}
LPWCH WINAPI x9_GetEnvironmentStringsW(void){LPCH e=GetEnvironmentStringsA();int n=0;LPWCH w;
 while(e[n]||e[n+1])n++;n+=2;w=(LPWCH)mem(n*2+2);MultiByteToWideChar(CP_ACP,0,e,n,w,n);FreeEnvironmentStringsA(e);return w;}
BOOL WINAPI x9_FreeEnvironmentStringsW(LPWCH w){unmem(w);return TRUE;}
DWORD WINAPI x9_ExpandEnvironmentStringsW(LPCWSTR s,LPWSTR d,DWORD cap){LPSTR a=A(s);DWORD k=ExpandEnvironmentStringsA(a,0,0),r;char*b=(char*)mem(k+1);
 ExpandEnvironmentStringsA(a,b,k+1);FREEA(a,s);r=a2wlen(b)+1;if(d&&cap>=r)a2wbuf(b,d,cap);unmem(b);return r;}
HANDLE WINAPI x9_CreateFileW(LPCWSTR n,DWORD acc,DWORD sh,LPSECURITY_ATTRIBUTES sa,DWORD cr,DWORD fl,HANDLE t){LPSTR a=A(n);HANDLE h=CreateFileA(a,acc,sh,sa,cr,fl,t);FREEA(a,n);return h;}
static void fd_a2w(const WIN32_FIND_DATAA*a,LPWIN32_FIND_DATAW w){w->dwFileAttributes=a->dwFileAttributes;w->ftCreationTime=a->ftCreationTime;w->ftLastAccessTime=a->ftLastAccessTime;
 w->ftLastWriteTime=a->ftLastWriteTime;w->nFileSizeHigh=a->nFileSizeHigh;w->nFileSizeLow=a->nFileSizeLow;w->dwReserved0=a->dwReserved0;w->dwReserved1=a->dwReserved1;
 a2wbuf(a->cFileName,w->cFileName,MAX_PATH);a2wbuf(a->cAlternateFileName,w->cAlternateFileName,14);}
HANDLE WINAPI x9_FindFirstFileW(LPCWSTR n,LPWIN32_FIND_DATAW w){WIN32_FIND_DATAA a;LPSTR s=A(n);HANDLE h=FindFirstFileA(s,&a);FREEA(s,n);if(h!=INVALID_HANDLE_VALUE)fd_a2w(&a,w);return h;}
BOOL WINAPI x9_FindNextFileW(HANDLE h,LPWIN32_FIND_DATAW w){WIN32_FIND_DATAA a;if(!FindNextFileA(h,&a))return FALSE;fd_a2w(&a,w);return TRUE;}
DWORD WINAPI x9_GetFileAttributesW(LPCWSTR n){LPSTR a=A(n);DWORD r=GetFileAttributesA(a);FREEA(a,n);return r;}
BOOL WINAPI x9_SetFileAttributesW(LPCWSTR n,DWORD v){LPSTR a=A(n);BOOL r=SetFileAttributesA(a,v);FREEA(a,n);return r;}
BOOL WINAPI x9_GetFileAttributesExW(LPCWSTR n,GET_FILEEX_INFO_LEVELS lv,LPVOID out){WIN32_FILE_ATTRIBUTE_DATA*o=(WIN32_FILE_ATTRIBUTE_DATA*)out;WIN32_FIND_DATAA fd;HANDLE h;LPSTR a;
 typedef BOOL(WINAPI*GA)(LPCSTR,GET_FILEEX_INFO_LEVELS,LPVOID);GA ga=(GA)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"GetFileAttributesExA");BOOL r;
 if(lv!=GetFileExInfoStandard||!o){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 a=A(n);if(ga){r=ga(a,lv,out);FREEA(a,n);return r;}
 h=FindFirstFileA(a,&fd);FREEA(a,n);if(h==INVALID_HANDLE_VALUE)return FALSE;FindClose(h);
 o->dwFileAttributes=fd.dwFileAttributes;o->ftCreationTime=fd.ftCreationTime;o->ftLastAccessTime=fd.ftLastAccessTime;o->ftLastWriteTime=fd.ftLastWriteTime;o->nFileSizeHigh=fd.nFileSizeHigh;o->nFileSizeLow=fd.nFileSizeLow;return TRUE;}
DWORD WINAPI x9_GetFullPathNameW(LPCWSTR n,DWORD cap,LPWSTR out,LPWSTR*part){char b[MAX_PATH*2];char*p=0;LPSTR a=A(n);DWORD k=GetFullPathNameA(a,sizeof(b),b,&p),r,i;FREEA(a,n);
 if(!k||k>=sizeof(b))return 0;r=a2wlen(b);if(!out||cap<=r)return r+1;a2wbuf(b,out,cap);
 if(part){*part=0;for(i=0;out[i];i++)if(out[i]=='\\')*part=out+i+1;if(*part&&!**part)*part=0;}return r;}
DWORD WINAPI x9_GetCurrentDirectoryW(DWORD cap,LPWSTR out){char b[MAX_PATH];DWORD k=GetCurrentDirectoryA(sizeof(b),b),r;if(!k)return 0;r=a2wlen(b);if(!out||cap<=r)return r+1;a2wbuf(b,out,cap);return r;}
BOOL WINAPI x9_SetCurrentDirectoryW(LPCWSTR n){LPSTR a=A(n);BOOL r=SetCurrentDirectoryA(a);FREEA(a,n);return r;}
DWORD WINAPI x9_GetTempPathW(DWORD cap,LPWSTR out){char b[MAX_PATH];DWORD k=GetTempPathA(sizeof(b),b),r;if(!k)return 0;r=a2wlen(b);if(!out||cap<=r)return r+1;a2wbuf(b,out,cap);return r;}
UINT WINAPI x9_GetTempFileNameW(LPCWSTR p,LPCWSTR pre,UINT u,LPWSTR out){char b[MAX_PATH];LPSTR a=A(p),c=A(pre);UINT r=GetTempFileNameA(a,c,u,b);FREEA(a,p);FREEA(c,pre);if(r)a2wbuf(b,out,MAX_PATH);return r;}
UINT WINAPI x9_GetSystemDirectoryW(LPWSTR out,UINT cap){char b[MAX_PATH];UINT k=GetSystemDirectoryA(b,sizeof(b)),r;if(!k)return 0;r=a2wlen(b);if(!out||cap<=r)return r+1;a2wbuf(b,out,cap);return r;}
UINT WINAPI x9_GetWindowsDirectoryW(LPWSTR out,UINT cap){char b[MAX_PATH];UINT k=GetWindowsDirectoryA(b,sizeof(b)),r;if(!k)return 0;r=a2wlen(b);if(!out||cap<=r)return r+1;a2wbuf(b,out,cap);return r;}
BOOL WINAPI x9_CreateDirectoryW(LPCWSTR n,LPSECURITY_ATTRIBUTES s){LPSTR a=A(n);BOOL r=CreateDirectoryA(a,s);FREEA(a,n);return r;}
BOOL WINAPI x9_RemoveDirectoryW(LPCWSTR n){LPSTR a=A(n);BOOL r=RemoveDirectoryA(a);FREEA(a,n);return r;}
BOOL WINAPI x9_DeleteFileW(LPCWSTR n){LPSTR a=A(n);BOOL r=DeleteFileA(a);FREEA(a,n);return r;}
BOOL WINAPI x9_CopyFileW(LPCWSTR s,LPCWSTR d,BOOL f){LPSTR a=A(s),b=A(d);BOOL r=CopyFileA(a,b,f);FREEA(a,s);FREEA(b,d);return r;}
BOOL WINAPI x9_MoveFileW(LPCWSTR s,LPCWSTR d){LPSTR a=A(s),b=A(d);BOOL r=MoveFileA(a,b);FREEA(a,s);FREEA(b,d);return r;}
/* Windows 9x has no MoveFileEx. */
BOOL WINAPI x9_MoveFileExW(LPCWSTR s,LPCWSTR d,DWORD fl){LPSTR a=A(s),b=A(d);BOOL r;
 if(!d){r=(fl&MOVEFILE_DELAY_UNTIL_REBOOT)?TRUE:DeleteFileA(a);FREEA(a,s);return r;}
 r=MoveFileA(a,b);
 if(!r&&(fl&MOVEFILE_REPLACE_EXISTING)&&GetFileAttributesA(b)!=0xFFFFFFFF){DeleteFileA(b);r=MoveFileA(a,b);}
 if(!r&&(fl&MOVEFILE_COPY_ALLOWED)){r=CopyFileA(a,b,!(fl&MOVEFILE_REPLACE_EXISTING));if(r)DeleteFileA(a);}
 FREEA(a,s);FREEA(b,d);return r;}
BOOL WINAPI x9_MoveFileExA(LPCSTR s,LPCSTR d,DWORD fl){LPWSTR a=W(s),b=W(d);BOOL r=x9_MoveFileExW(a,b,fl);FA(a,s);FA(b,d);return r;}
BOOL WINAPI x9_GetVolumeInformationW(LPCWSTR root,LPWSTR name,DWORD ncap,LPDWORD serial,LPDWORD maxc,LPDWORD flags,LPWSTR fs,DWORD fscap){char nb[MAX_PATH],fb[MAX_PATH];LPSTR a=A(root);
 BOOL r=GetVolumeInformationA(a,nb,sizeof(nb),serial,maxc,flags,fb,sizeof(fb));FREEA(a,root);if(r){if(name)a2wbuf(nb,name,ncap);if(fs)a2wbuf(fb,fs,fscap);}return r;}
HANDLE WINAPI x9_CreateEventW(LPSECURITY_ATTRIBUTES s,BOOL m,BOOL i,LPCWSTR n){LPSTR a=A(n);HANDLE h=CreateEventA(s,m,i,a);FREEA(a,n);return h;}
HANDLE WINAPI x9_CreateMutexW(LPSECURITY_ATTRIBUTES s,BOOL o,LPCWSTR n){LPSTR a=A(n);HANDLE h=CreateMutexA(s,o,a);FREEA(a,n);return h;}
HANDLE WINAPI x9_CreateSemaphoreW(LPSECURITY_ATTRIBUTES s,LONG i,LONG m,LPCWSTR n){LPSTR a=A(n);HANDLE h=CreateSemaphoreA(s,i,m,a);FREEA(a,n);return h;}
HANDLE WINAPI x9_CreateFileMappingW(HANDLE f,LPSECURITY_ATTRIBUTES s,DWORD p,DWORD hi,DWORD lo,LPCWSTR n){LPSTR a=A(n);HANDLE h=CreateFileMappingA(f,s,p,hi,lo,a);FREEA(a,n);return h;}
VOID WINAPI x9_OutputDebugStringW(LPCWSTR s){LPSTR a=A(s);if(a)OutputDebugStringA(a);FREEA(a,s);}
HRSRC WINAPI x9_FindResourceW(HMODULE m,LPCWSTR n,LPCWSTR t){LPSTR a=A(n),b=A(t);HRSRC r=FindResourceA(m,a,b);FREEA(a,n);FREEA(b,t);return r;}
HRSRC WINAPI x9_FindResourceExW(HMODULE m,LPCWSTR t,LPCWSTR n,WORD l){LPSTR a=A(n),b=A(t);HRSRC r=FindResourceExA(m,b,a,l);FREEA(a,n);FREEA(b,t);return r;}
DWORD WINAPI x9_FormatMessageW(DWORD fl,LPCVOID src,DWORD id,DWORD lang,LPWSTR buf,DWORD cap,va_list*args){char*a=0;DWORD n,r;LPSTR s=(fl&FORMAT_MESSAGE_FROM_STRING)?A((LPCWSTR)src):(LPSTR)src;
 /* Inserts are passed through unchanged; Flash only formats system error text. */
 n=FormatMessageA(fl|FORMAT_MESSAGE_ALLOCATE_BUFFER,s,id,lang,(LPSTR)&a,0,args);
 if(fl&FORMAT_MESSAGE_FROM_STRING)FREEA(s,src);if(!n||!a)return 0;r=a2wlen(a);
 if(fl&FORMAT_MESSAGE_ALLOCATE_BUFFER){LPWSTR w=(LPWSTR)LocalAlloc(LMEM_FIXED,(r+1)*2);if(!w){LocalFree(a);return 0;}a2wbuf(a,w,r+1);*(LPWSTR*)buf=w;}
 else{if(cap<=r){LocalFree(a);SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}a2wbuf(a,buf,cap);}
 LocalFree(a);return r;}
int WINAPI x9_GetLocaleInfoW(LCID l,LCTYPE t,LPWSTR out,int cap){char b[256];int n=GetLocaleInfoA(l,t,b,sizeof(b)),r;if(!n)return 0;
 if(t&LOCALE_RETURN_NUMBER){if(out&&cap>=2)*(DWORD*)out=*(DWORD*)b;return 2;}
 r=a2wlen(b)+1;if(!cap||!out)return r;if(cap<r){SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}a2wbuf(b,out,cap);return r;}
static LPSTR An(LPCWSTR w,int n,int*outn){int k;LPSTR a;if(n<0)n=wlen(w)+1;k=WideCharToMultiByte(CP_ACP,0,w,n,0,0,0,0);a=(LPSTR)mem(k+1);WideCharToMultiByte(CP_ACP,0,w,n,a,k,0,0);*outn=k;return a;}
int WINAPI x9_CompareStringW(LCID l,DWORD f,LPCWSTR a,int an,LPCWSTR b,int bn){int x,y,r;LPSTR p=An(a,an,&x),q=An(b,bn,&y);
 if(an<0)x=-1;if(bn<0)y=-1;r=CompareStringA(l,f,p,x,q,y);unmem(p);unmem(q);return r;}
int WINAPI x9_LCMapStringW(LCID l,DWORD f,LPCWSTR s,int n,LPWSTR d,int cap){int k,r,i;LPWSTR o;
 if(f&LCMAP_SORTKEY){LPSTR a=An(s,n,&k);r=LCMapStringA(l,f,a,n<0?-1:k,(LPSTR)d,cap);unmem(a);return r;}
 if(n<0)n=wlen(s)+1;if(!cap)return n;if(cap<n){SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}
 o=(LPWSTR)mem(n*2+2);for(i=0;i<n;i++)o[i]=s[i];
 if(f&LCMAP_UPPERCASE)x9_CharUpperBuffW(o,n);else if(f&LCMAP_LOWERCASE)x9_CharLowerBuffW(o,n);
 for(i=0;i<n;i++)d[i]=o[i];unmem(o);return n;}
/* Unicode character classes for Latin/Latin-1 and generic fallback. */
static WORD ctype1(WCHAR c){WORD t=0;
 if(c<128){if(c>='0'&&c<='9')t|=C1_DIGIT|C1_XDIGIT;if((c>='a'&&c<='f')||(c>='A'&&c<='F'))t|=C1_XDIGIT;
  if(c>='a'&&c<='z')t|=C1_LOWER|C1_ALPHA;if(c>='A'&&c<='Z')t|=C1_UPPER|C1_ALPHA;
  if(c==' '||(c>=9&&c<=13))t|=C1_SPACE;if(c==' '||c=='\t')t|=C1_BLANK;if(c<32||c==127)t|=C1_CNTRL;
  if(c>32&&c<127&&!(t&(C1_ALPHA|C1_DIGIT)))t|=C1_PUNCT;return t;}
 if(c==0xA0){return C1_SPACE|C1_BLANK;}
 if(c>=0xC0&&c<=0x24F&&c!=0xD7&&c!=0xF7){t|=C1_ALPHA;if(c<0x100)t|=(c>=0xE0)?C1_LOWER:C1_UPPER;return t;}
 if(c<0xC0)return C1_PUNCT;
 if((c>=0x370&&c<=0x3FF)||(c>=0x400&&c<=0x52F)||(c>=0x5D0&&c<=0x6FF)||(c>=0x3040&&c<=0x30FF)||(c>=0x4E00&&c<=0x9FFF)||(c>=0xAC00&&c<=0xD7A3))return C1_ALPHA;
 return C1_PUNCT;}
BOOL WINAPI x9_GetStringTypeW(DWORD type,LPCWSTR s,int n,LPWORD out){int i;if(n<0)n=wlen(s)+1;
 for(i=0;i<n;i++)out[i]=type==CT_CTYPE1?ctype1(s[i]):(type==CT_CTYPE2?(s[i]>='0'&&s[i]<='9'?C2_EUROPENUMBER:C2_LEFTTORIGHT):0);return TRUE;}
BOOL WINAPI x9_GetStringTypeExW(LCID l,DWORD type,LPCWSTR s,int n,LPWORD out){(void)l;return x9_GetStringTypeW(type,s,n,out);}
int WINAPI x9_GetDateFormatW(LCID l,DWORD f,const SYSTEMTIME*t,LPCWSTR fmt,LPWSTR out,int cap){char b[256];LPSTR a=A(fmt);int n=GetDateFormatA(l,f,t,a,b,sizeof(b)),r;FREEA(a,fmt);if(!n)return 0;r=a2wlen(b)+1;if(!cap)return r;if(cap<r)return 0;a2wbuf(b,out,cap);return r;}
int WINAPI x9_GetTimeFormatW(LCID l,DWORD f,const SYSTEMTIME*t,LPCWSTR fmt,LPWSTR out,int cap){char b[256];LPSTR a=A(fmt);int n=GetTimeFormatA(l,f,t,a,b,sizeof(b)),r;FREEA(a,fmt);if(!n)return 0;r=a2wlen(b)+1;if(!cap)return r;if(cap<r)return 0;a2wbuf(b,out,cap);return r;}
int WINAPI x9_lstrlenW(LPCWSTR s){return wlen(s);}
LPWSTR WINAPI x9_lstrcpyW(LPWSTR d,LPCWSTR s){int i=0;if(!d)return 0;if(s)while((d[i]=s[i])!=0)i++;else d[0]=0;return d;}
LPWSTR WINAPI x9_lstrcatW(LPWSTR d,LPCWSTR s){x9_lstrcpyW(d+wlen(d),s);return d;}
LPWSTR WINAPI x9_lstrcpynW(LPWSTR d,LPCWSTR s,int n){int i;if(!d||n<=0)return d;for(i=0;i<n-1&&s&&s[i];i++)d[i]=s[i];d[i]=0;return d;}
int WINAPI x9_lstrcmpW(LPCWSTR a,LPCWSTR b){int i=0;if(!a)a=L"";if(!b)b=L"";while(a[i]&&a[i]==b[i])i++;return a[i]<b[i]?-1:a[i]>b[i];}
static WCHAR lowc(WCHAR c){if(c>='A'&&c<='Z')return c+32;if(c>=0xC0&&c<=0xDE&&c!=0xD7)return c+32;return c;}
int WINAPI x9_lstrcmpiW(LPCWSTR a,LPCWSTR b){int i=0;WCHAR x,y;if(!a)a=L"";if(!b)b=L"";for(;;i++){x=lowc(a[i]);y=lowc(b[i]);if(x!=y||!x)return x<y?-1:x>y;}}
DWORD WINAPI x9_SearchPathW(LPCWSTR p,LPCWSTR f,LPCWSTR e,DWORD cap,LPWSTR out,LPWSTR*part){char b[MAX_PATH];char*pp=0;LPSTR a=A(p),c=A(f),d=A(e);DWORD k=SearchPathA(a,c,d,sizeof(b),b,&pp),r,i;
 FREEA(a,p);FREEA(c,f);FREEA(d,e);if(!k)return 0;r=a2wlen(b);if(!out||cap<=r)return r+1;a2wbuf(b,out,cap);if(part){*part=0;for(i=0;out[i];i++)if(out[i]=='\\')*part=out+i+1;}return r;}
DWORD WINAPI x9_GetPrivateProfileStringW(LPCWSTR s,LPCWSTR k,LPCWSTR d,LPWSTR out,DWORD cap,LPCWSTR f){LPSTR a=A(s),b=A(k),c=A(d),e=A(f);char*t=(char*)mem(cap*2+2);DWORD n=GetPrivateProfileStringA(a,b,c,t,cap*2+1,e),r;
 FREEA(a,s);FREEA(b,k);FREEA(c,d);FREEA(e,f);r=MultiByteToWideChar(CP_ACP,0,t,n+1,out,cap);unmem(t);return r?r-1:0;}
DWORD WINAPI x9_GetProfileStringW(LPCWSTR s,LPCWSTR k,LPCWSTR d,LPWSTR out,DWORD cap){LPSTR a=A(s),b=A(k),c=A(d);char*t=(char*)mem(cap*2+2);DWORD n=GetProfileStringA(a,b,c,t,cap*2+1),r;
 FREEA(a,s);FREEA(b,k);FREEA(c,d);r=MultiByteToWideChar(CP_ACP,0,t,n+1,out,cap);unmem(t);return r?r-1:0;}
BOOL WINAPI x9_WriteConsoleW(HANDLE h,const VOID*s,DWORD n,LPDWORD w,LPVOID r){int k;LPSTR a=An((LPCWSTR)s,n,&k);BOOL ok=WriteConsoleA(h,a,k,w,r);unmem(a);if(w)*w=n;return ok;}
BOOL WINAPI x9_CreateProcessW(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,BOOL inh,DWORD fl,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){
 STARTUPINFOA s;LPSTR a=A(app),c=A(cmd),d=A(dir),t=si?A(si->lpTitle):0,de=si?A(si->lpDesktop):0;BOOL r;
 s.cb=sizeof(s);s.lpReserved=0;s.lpDesktop=de;s.lpTitle=t;s.dwX=si->dwX;s.dwY=si->dwY;s.dwXSize=si->dwXSize;s.dwYSize=si->dwYSize;s.dwXCountChars=si->dwXCountChars;s.dwYCountChars=si->dwYCountChars;
 s.dwFillAttribute=si->dwFillAttribute;s.dwFlags=si->dwFlags;s.wShowWindow=si->wShowWindow;s.cbReserved2=0;s.lpReserved2=0;s.hStdInput=si->hStdInput;s.hStdOutput=si->hStdOutput;s.hStdError=si->hStdError;
 r=CreateProcessA(a,c,pa,ta,inh,fl&~CREATE_UNICODE_ENVIRONMENT,(fl&CREATE_UNICODE_ENVIRONMENT)?0:env,d,&s,pi);
 FREEA(a,app);FREEA(c,cmd);FREEA(d,dir);if(si){FREEA(t,si->lpTitle);FREEA(de,si->lpDesktop);}return r;}
BOOL WINAPI x9_GetVersionExW(LPOSVERSIONINFOW v){OSVERSIONINFOA a;if(!v)return FALSE;a.dwOSVersionInfoSize=sizeof(a);if(!GetVersionExA(&a))return FALSE;
 v->dwMajorVersion=a.dwMajorVersion;v->dwMinorVersion=a.dwMinorVersion;v->dwBuildNumber=a.dwBuildNumber;v->dwPlatformId=a.dwPlatformId;a2wbuf(a.szCSDVersion,v->szCSDVersion,128);
 if(v->dwOSVersionInfoSize>=sizeof(OSVERSIONINFOEXW)){OSVERSIONINFOEXW*x=(OSVERSIONINFOEXW*)v;x->wServicePackMajor=0;x->wServicePackMinor=0;x->wSuiteMask=0;x->wProductType=VER_NT_WORKSTATION;x->wReserved=0;}return TRUE;}
/* LoadLibrary hooks: load, then patch the new module's imports. */
static void patch_all(void);static void*lookup_name(const char*n);
HMODULE WINAPI x9_LoadLibraryA(LPCSTR n){HMODULE h=LoadLibraryA(n);if(h)patch_all();return h;}
HMODULE WINAPI x9_LoadLibraryExA(LPCSTR n,HANDLE f,DWORD fl){HMODULE h=LoadLibraryExA(n,f,fl);if(h&&!(fl&LOAD_LIBRARY_AS_DATAFILE))patch_all();return h;}
HMODULE WINAPI x9_LoadLibraryW(LPCWSTR n){LPSTR a=A(n);HMODULE h=x9_LoadLibraryA(a);FREEA(a,n);return h;}
HMODULE WINAPI x9_LoadLibraryExW(LPCWSTR n,HANDLE f,DWORD fl){LPSTR a=A(n);HMODULE h=x9_LoadLibraryExA(a,f,fl);FREEA(a,n);return h;}

/* ------------------------------------------------------------------ */
/* advapi32                                                            */
LONG WINAPI x9_RegOpenKeyExW(HKEY k,LPCWSTR s,DWORD o,REGSAM m,PHKEY r){LPSTR a=A(s);LONG e=RegOpenKeyExA(k,a,o,m,r);FREEA(a,s);return e;}
LONG WINAPI x9_RegOpenKeyW(HKEY k,LPCWSTR s,PHKEY r){LPSTR a=A(s);LONG e=RegOpenKeyA(k,a,r);FREEA(a,s);return e;}
LONG WINAPI x9_RegCreateKeyExW(HKEY k,LPCWSTR s,DWORD res,LPWSTR cls,DWORD o,REGSAM m,LPSECURITY_ATTRIBUTES sa,PHKEY r,LPDWORD d){LPSTR a=A(s),c=A(cls);LONG e=RegCreateKeyExA(k,a,res,c,o,m,sa,r,d);FREEA(a,s);FREEA(c,cls);return e;}
LONG WINAPI x9_RegDeleteKeyW(HKEY k,LPCWSTR s){LPSTR a=A(s);LONG e=RegDeleteKeyA(k,a);FREEA(a,s);return e;}
LONG WINAPI x9_RegDeleteValueW(HKEY k,LPCWSTR s){LPSTR a=A(s);LONG e=RegDeleteValueA(k,a);FREEA(a,s);return e;}
static BOOL is_str(DWORD t){return t==REG_SZ||t==REG_EXPAND_SZ||t==REG_MULTI_SZ;}
LONG WINAPI x9_RegQueryValueExW(HKEY k,LPCWSTR n,LPDWORD res,LPDWORD type,LPBYTE data,LPDWORD cb){LPSTR a=A(n);DWORD t=0,sz=0,need;LONG e;BYTE*b;
 e=RegQueryValueExA(k,a,res,&t,0,&sz);if(e){FREEA(a,n);return e;}if(type)*type=t;
 if(!is_str(t)){e=RegQueryValueExA(k,a,res,0,data,cb);FREEA(a,n);return e;}
 b=(BYTE*)mem(sz+2);e=RegQueryValueExA(k,a,res,0,b,&sz);FREEA(a,n);if(e){unmem(b);return e;}
 need=MultiByteToWideChar(CP_ACP,0,(LPCSTR)b,sz,0,0)*2;
 if(!cb){unmem(b);return ERROR_INVALID_PARAMETER;}
 if(!data){*cb=need;unmem(b);return ERROR_SUCCESS;}
 if(*cb<need){*cb=need;unmem(b);return ERROR_MORE_DATA;}
 MultiByteToWideChar(CP_ACP,0,(LPCSTR)b,sz,(LPWSTR)data,need/2);*cb=need;unmem(b);return ERROR_SUCCESS;}
LONG WINAPI x9_RegSetValueExW(HKEY k,LPCWSTR n,DWORD res,DWORD t,const BYTE*data,DWORD cb){LPSTR a=A(n);LONG e;
 if(is_str(t)&&data){int m;LPSTR s=An((LPCWSTR)data,cb/2,&m);e=RegSetValueExA(k,a,res,t,(BYTE*)s,m);unmem(s);}
 else e=RegSetValueExA(k,a,res,t,data,cb);FREEA(a,n);return e;}
LONG WINAPI x9_RegEnumKeyExW(HKEY k,DWORD i,LPWSTR n,LPDWORD nc,LPDWORD r,LPWSTR c,LPDWORD cc,PFILETIME ft){char b[MAX_PATH];DWORD bl=sizeof(b);LONG e=RegEnumKeyExA(k,i,b,&bl,r,0,0,ft);
 if(e)return e;if(*nc<=(DWORD)a2wlen(b)){*nc=a2wlen(b)+1;return ERROR_MORE_DATA;}*nc=a2wbuf(b,n,*nc);if(cc)*cc=0;(void)c;return 0;}

/* ------------------------------------------------------------------ */
/* window procedure thunks                                             */
typedef struct Thunk { BYTE code[16]; WNDPROC wproc; struct Thunk*next; } Thunk;
static Thunk*thunks;static CRITICAL_SECTION tlock;static BYTE*tpool;static int tleft;
LRESULT WINAPI x9_dispatchW(WNDPROC wp,HWND h,UINT m,WPARAM w,LPARAM l);
INT_PTR WINAPI x9_dispatchDlg(WNDPROC wp,HWND h,UINT m,WPARAM w,LPARAM l);
static Thunk*thunk_of(WNDPROC wp,BOOL dlg){Thunk*t;BYTE*c;DWORD target;
 if(!wp)return 0;EnterCriticalSection(&tlock);
 for(t=thunks;t;t=t->next)if(t->wproc==wp&&(t->code[15]==(BYTE)dlg)){LeaveCriticalSection(&tlock);return t;}
 if(tleft<(int)sizeof(Thunk)){tpool=(BYTE*)VirtualAlloc(0,65536,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);tleft=tpool?65536:0;}
 if(!tpool){LeaveCriticalSection(&tlock);return 0;}
 t=(Thunk*)tpool;tpool+=sizeof(Thunk);tleft-=sizeof(Thunk);
 c=t->code;target=(DWORD)(dlg?(void*)x9_dispatchDlg:(void*)x9_dispatchW);
 /* pop ecx ; push wproc ; push ecx ; jmp dispatch */
 c[0]=0x59;c[1]=0x68;*(DWORD*)(c+2)=(DWORD)wp;c[6]=0x51;c[7]=0xE9;*(DWORD*)(c+8)=target-(DWORD)(c+12);c[12]=0x90;c[13]=0x90;c[14]=0x90;c[15]=(BYTE)dlg;
 t->wproc=wp;t->next=thunks;thunks=t;LeaveCriticalSection(&tlock);return t;}
static WNDPROC wproc_of(LONG_PTR p){Thunk*t;EnterCriticalSection(&tlock);for(t=thunks;t;t=t->next)if((LONG_PTR)t==p){LeaveCriticalSection(&tlock);return t->wproc;}LeaveCriticalSection(&tlock);return 0;}

/* ANSI character from a WM_CHAR-type wParam -> Unicode */
static WPARAM chr_a2w(WPARAM w){char b[2];WCHAR c=0;b[0]=(char)(w&0xFF);b[1]=0;if((w&0xFF)<0x80)return w;
 if(IsDBCSLeadByte((BYTE)(w&0xFF))&&(w&0xFF00)){b[0]=(char)(w>>8);b[1]=(char)w;MultiByteToWideChar(CP_ACP,0,b,2,&c,1);}else MultiByteToWideChar(CP_ACP,0,b,1,&c,1);return c?c:w;}
static WPARAM chr_w2a(WPARAM w){char b[3];WCHAR c=(WCHAR)w;int n;if(w<0x80)return w;n=WideCharToMultiByte(CP_ACP,0,&c,1,b,2,0,0);if(n==2)return ((BYTE)b[0]<<8)|(BYTE)b[1];return n?(BYTE)b[0]:'?';}
static BOOL is_char_msg(UINT m){return m==WM_CHAR||m==WM_DEADCHAR||m==WM_SYSCHAR||m==WM_SYSDEADCHAR||m==WM_IME_CHAR||m==WM_MENUCHAR;}
typedef struct { CREATESTRUCTW cs; } CsW;

/* Call a W procedure for an ANSI message. */
static LRESULT call_w(WNDPROC wp,HWND h,UINT m,WPARAM w,LPARAM l,BOOL dlg){LRESULT r;if(m==WM_NCCREATE||m==WM_SETTEXT)lg("thunk %x -> %x msg %x",h,wp,m);
 if(is_char_msg(m)){if(m==WM_MENUCHAR)w=MAKEWPARAM(chr_a2w(LOWORD(w)),HIWORD(w));else w=chr_a2w(w);return wp(h,m,w,l);}
 if((m==WM_CREATE||m==WM_NCCREATE)&&l){CREATESTRUCTA*a=(CREATESTRUCTA*)l;CREATESTRUCTW c;LPWSTR n=W(a->lpszName),k=W(a->lpszClass);
  c.lpCreateParams=a->lpCreateParams;c.hInstance=a->hInstance;c.hMenu=a->hMenu;c.hwndParent=a->hwndParent;c.cy=a->cy;c.cx=a->cx;c.y=a->y;c.x=a->x;c.style=a->style;c.lpszName=n;c.lpszClass=k;c.dwExStyle=a->dwExStyle;
  r=wp(h,m,w,(LPARAM)&c);FA(n,a->lpszName);FA(k,a->lpszClass);return r;}
 if(m==WM_SETTEXT&&l){LPWSTR s=W((LPCSTR)l);r=wp(h,m,w,(LPARAM)s);FA(s,(LPCSTR)l);return r;}
 if(m==WM_SETTINGCHANGE&&l&&!is_res((void*)l)){LPWSTR s=W((LPCSTR)l);r=wp(h,m,w,(LPARAM)s);FA(s,(LPCSTR)l);return r;}
 if(m==WM_GETTEXT&&l){int cap=(int)w;LPWSTR b=(LPWSTR)mem(cap*2+2);int n;r=wp(h,m,cap,(LPARAM)b);b[cap>0?cap-1:0]=0;n=w2abuf(b,(LPSTR)l,cap);unmem(b);return n;}
 (void)dlg;return wp(h,m,w,l);}
LRESULT WINAPI x9_dispatchW(WNDPROC wp,HWND h,UINT m,WPARAM w,LPARAM l){return call_w(wp,h,m,w,l,FALSE);}
INT_PTR WINAPI x9_dispatchDlg(WNDPROC wp,HWND h,UINT m,WPARAM w,LPARAM l){return (INT_PTR)call_w(wp,h,m,w,l,TRUE);}

/* Call an ANSI procedure (or DefWindowProcA) for a W message. */
typedef LRESULT (WINAPI*AProc)(HWND,UINT,WPARAM,LPARAM);
static LRESULT call_a(AProc ap,WNDPROC prev,HWND h,UINT m,WPARAM w,LPARAM l){LRESULT r;
#define CALLA(W_,L_) (prev?CallWindowProcA((WNDPROC)prev,h,m,W_,L_):ap(h,m,W_,L_))
 if(is_char_msg(m)){if(m==WM_MENUCHAR)w=MAKEWPARAM(chr_w2a(LOWORD(w)),HIWORD(w));else w=chr_w2a(w);return CALLA(w,l);}
 if((m==WM_CREATE||m==WM_NCCREATE)&&l){CREATESTRUCTW*c=(CREATESTRUCTW*)l;CREATESTRUCTA a;LPSTR n=A(c->lpszName),k=A(c->lpszClass);
  a.lpCreateParams=c->lpCreateParams;a.hInstance=c->hInstance;a.hMenu=c->hMenu;a.hwndParent=c->hwndParent;a.cy=c->cy;a.cx=c->cx;a.y=c->y;a.x=c->x;a.style=c->style;a.lpszName=n;a.lpszClass=k;a.dwExStyle=c->dwExStyle;
  r=CALLA(w,(LPARAM)&a);FREEA(n,c->lpszName);FREEA(k,c->lpszClass);return r;}
 if((m==WM_SETTEXT||m==WM_SETTINGCHANGE)&&l&&!is_res((void*)l)){LPSTR s=A((LPCWSTR)l);r=CALLA(w,(LPARAM)s);FREEA(s,(LPCWSTR)l);return r;}
 if(m==WM_GETTEXT&&l){int cap=(int)w;char*b=(char*)mem(cap*2+2);r=CALLA(cap*2,(LPARAM)b);if(cap>0){a2wbuf(b,(LPWSTR)l,cap);r=wlen((LPWSTR)l);}unmem(b);return r;}
 return CALLA(w,l);
#undef CALLA
}
LRESULT WINAPI x9_DefWindowProcW(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_NCCREATE||m==WM_SETTEXT)lg("DefWindowProcW %x msg %x",h,m);return call_a(DefWindowProcA,0,h,m,w,l);}
LRESULT WINAPI x9_DefDlgProcW(HWND h,UINT m,WPARAM w,LPARAM l){return call_a(DefDlgProcA,0,h,m,w,l);}
LRESULT WINAPI x9_DefFrameProcW(HWND h,HWND c,UINT m,WPARAM w,LPARAM l){return DefFrameProcA(h,c,m,w,l);}
static BOOL is_wproc(WNDPROC p){Thunk*t;BOOL r=FALSE;EnterCriticalSection(&tlock);for(t=thunks;t;t=t->next)if(t->wproc==p){r=TRUE;break;}LeaveCriticalSection(&tlock);return r;}
/* Is p an ANSI window procedure? Procedures of system DLLs (and the 9x
   shared arena / 16-bit thunks above 2 GB) are ANSI; everything in the
   player, its proxies or heap thunks is Unicode code. */
static BOOL system_handle(HMODULE m);
static BOOL ansi_proc(WNDPROC p){MEMORY_BASIC_INFORMATION mb;
 if(is_wproc(p))return FALSE;
 if((DWORD)p>=0x80000000u)return TRUE;
 if(VirtualQuery((LPCVOID)p,&mb,sizeof(mb))&&mb.AllocationBase&&system_handle((HMODULE)mb.AllocationBase))return TRUE;
 return FALSE;}
LRESULT WINAPI x9_CallWindowProcW(WNDPROC p,HWND h,UINT m,WPARAM w,LPARAM l){WNDPROC wp=wproc_of((LONG_PTR)p);
 if(wp)return wp(h,m,w,l);                       /* our thunk: call the W procedure directly */
 if(!ansi_proc(p))return p(h,m,w,l);             /* application (Unicode) procedure */
 return call_a(0,p,h,m,w,l);}                    /* ANSI procedure (e.g. a system control) */
LONG WINAPI x9_SetWindowLongW(HWND h,int i,LONG v){LONG o,ow;
 if(i==GWL_WNDPROC){Thunk*t=wproc_of(v)?(Thunk*)v:thunk_of((WNDPROC)v,FALSE);o=SetWindowLongA(h,i,(LONG)t);ow=(LONG)wproc_of(o);return ow?ow:o;}
 if(i==DWL_DLGPROC&&GetClassLongA(h,GCW_ATOM)==32770){Thunk*t=thunk_of((WNDPROC)v,TRUE);o=SetWindowLongA(h,i,(LONG)t);ow=(LONG)wproc_of(o);return ow?ow:o;}
 return SetWindowLongA(h,i,v);}
LONG WINAPI x9_GetWindowLongW(HWND h,int i){LONG o=GetWindowLongA(h,i),w;if(i==GWL_WNDPROC||i==DWL_DLGPROC){w=(LONG)wproc_of(o);if(w)return w;}return o;}
DWORD WINAPI x9_GetClassLongW(HWND h,int i){DWORD o=GetClassLongA(h,i),w;if(i==GCL_WNDPROC){w=(DWORD)wproc_of(o);if(w)return w;}return o;}
DWORD WINAPI x9_SetClassLongW(HWND h,int i,LONG v){if(i==GCL_WNDPROC){Thunk*t=thunk_of((WNDPROC)v,FALSE);DWORD o=SetClassLongA(h,i,(LONG)t),w=(DWORD)wproc_of(o);return w?w:o;}return SetClassLongA(h,i,v);}
ATOM WINAPI x9_RegisterClassExW(const WNDCLASSEXW*c){WNDCLASSEXA a;ATOM r;lg("RegisterClassExW %s proc %x",dbg(c->lpszClassName),c->lpfnWndProc);LPSTR n=A(c->lpszClassName),mn=A(c->lpszMenuName);Thunk*t=thunk_of(c->lpfnWndProc,FALSE);
 typedef ATOM(WINAPI*RX)(const WNDCLASSEXA*);RX rx=(RX)GetProcAddress(GetModuleHandleA("USER32.DLL"),"RegisterClassExA");
 a.cbSize=sizeof(a);a.style=c->style;a.lpfnWndProc=(WNDPROC)t;a.cbClsExtra=c->cbClsExtra;a.cbWndExtra=c->cbWndExtra;a.hInstance=c->hInstance;a.hIcon=c->hIcon;a.hCursor=c->hCursor;
 a.hbrBackground=c->hbrBackground;a.lpszMenuName=mn;a.lpszClassName=n;a.hIconSm=c->cbSize>=sizeof(WNDCLASSEXW)?c->hIconSm:0;
 if(rx)r=rx(&a);else{WNDCLASSA b;b.style=a.style;b.lpfnWndProc=a.lpfnWndProc;b.cbClsExtra=a.cbClsExtra;b.cbWndExtra=a.cbWndExtra;b.hInstance=a.hInstance;b.hIcon=a.hIcon;b.hCursor=a.hCursor;b.hbrBackground=a.hbrBackground;b.lpszMenuName=a.lpszMenuName;b.lpszClassName=a.lpszClassName;r=RegisterClassA(&b);}
 FREEA(n,c->lpszClassName);FREEA(mn,c->lpszMenuName);return r;}
ATOM WINAPI x9_RegisterClassW(const WNDCLASSW*c){WNDCLASSEXW x;x.cbSize=sizeof(x);x.style=c->style;x.lpfnWndProc=c->lpfnWndProc;x.cbClsExtra=c->cbClsExtra;x.cbWndExtra=c->cbWndExtra;x.hInstance=c->hInstance;
 x.hIcon=c->hIcon;x.hCursor=c->hCursor;x.hbrBackground=c->hbrBackground;x.lpszMenuName=c->lpszMenuName;x.lpszClassName=c->lpszClassName;x.hIconSm=0;return x9_RegisterClassExW(&x);}
BOOL WINAPI x9_UnregisterClassW(LPCWSTR n,HINSTANCE h){LPSTR a=A(n);BOOL r=UnregisterClassA(a,h);FREEA(a,n);return r;}
BOOL WINAPI x9_GetClassInfoExW(HINSTANCE h,LPCWSTR n,LPWNDCLASSEXW c){WNDCLASSA a;LPSTR s=A(n);BOOL r=GetClassInfoA(h,s,&a);WNDPROC w;FREEA(s,n);if(!r)return FALSE;
 w=wproc_of((LONG_PTR)a.lpfnWndProc);c->cbSize=sizeof(*c);c->style=a.style;c->lpfnWndProc=w?w:a.lpfnWndProc;c->cbClsExtra=a.cbClsExtra;c->cbWndExtra=a.cbWndExtra;c->hInstance=a.hInstance;
 c->hIcon=a.hIcon;c->hCursor=a.hCursor;c->hbrBackground=a.hbrBackground;c->lpszMenuName=(LPCWSTR)a.lpszMenuName;c->lpszClassName=n;c->hIconSm=0;return TRUE;}
BOOL WINAPI x9_GetClassInfoW(HINSTANCE h,LPCWSTR n,LPWNDCLASSW c){WNDCLASSEXW x;if(!x9_GetClassInfoExW(h,n,&x))return FALSE;c->style=x.style;c->lpfnWndProc=x.lpfnWndProc;c->cbClsExtra=x.cbClsExtra;
 c->cbWndExtra=x.cbWndExtra;c->hInstance=x.hInstance;c->hIcon=x.hIcon;c->hCursor=x.hCursor;c->hbrBackground=x.hbrBackground;c->lpszMenuName=x.lpszMenuName;c->lpszClassName=x.lpszClassName;return TRUE;}
int WINAPI x9_GetClassNameW(HWND h,LPWSTR out,int cap){char b[256];int n=GetClassNameA(h,b,sizeof(b));if(!n)return 0;return a2wbuf(b,out,cap);}
HWND WINAPI x9_CreateWindowExW(DWORD ex,LPCWSTR cls,LPCWSTR name,DWORD st,int x,int y,int w,int h,HWND p,HMENU m,HINSTANCE i,LPVOID lp){LPSTR a=A(cls),b=A(name);
 HWND r=CreateWindowExA(ex,a,b,st,x,y,w,h,p,m,i,lp);lg("CreateWindowExW %s '%s' -> %x",dbg(cls),dbg(name),r);FREEA(a,cls);FREEA(b,name);return r;}
BOOL WINAPI x9_SetWindowTextW(HWND h,LPCWSTR s){lg("SetWindowTextW %x '%s'",h,dbg(s));LPSTR a=A(s);BOOL r=SetWindowTextA(h,a);FREEA(a,s);return r;}
int WINAPI x9_GetWindowTextLengthW(HWND h){int n=GetWindowTextLengthA(h);char*b;int r;if(n<=0)return n;b=(char*)mem(n+2);GetWindowTextA(h,b,n+1);r=a2wlen(b);unmem(b);return r;}
int WINAPI x9_GetWindowTextW(HWND h,LPWSTR out,int cap){int n=GetWindowTextLengthA(h);char*b;int r;if(!out||cap<=0)return 0;b=(char*)mem(n+2);GetWindowTextA(h,b,n+1);r=a2wbuf(b,out,cap);unmem(b);return r;}
BOOL WINAPI x9_SetDlgItemTextW(HWND h,int id,LPCWSTR s){LPSTR a=A(s);BOOL r=SetDlgItemTextA(h,id,a);FREEA(a,s);return r;}
UINT WINAPI x9_GetDlgItemTextW(HWND h,int id,LPWSTR out,int cap){char*b;UINT r;if(!out||cap<=0)return 0;b=(char*)mem(cap*2+2);GetDlgItemTextA(h,id,b,cap*2+1);r=a2wbuf(b,out,cap);unmem(b);return r;}
/* messages */
static void msg_a2w(LPMSG m){if(is_char_msg(m->message)&&m->message!=WM_MENUCHAR)m->wParam=chr_a2w(m->wParam);}
static void msg_w2a(MSG*d,const MSG*s){*d=*s;if(is_char_msg(s->message)&&s->message!=WM_MENUCHAR)d->wParam=chr_w2a(s->wParam);}
BOOL WINAPI x9_GetMessageW(LPMSG m,HWND h,UINT a,UINT b){BOOL r=GetMessageA(m,h,a,b);if(r>0)msg_a2w(m);return r;}
BOOL WINAPI x9_PeekMessageW(LPMSG m,HWND h,UINT a,UINT b,UINT f){BOOL r=PeekMessageA(m,h,a,b,f);if(r)msg_a2w(m);return r;}
LRESULT WINAPI x9_DispatchMessageW(const MSG*m){MSG a;msg_w2a(&a,m);return DispatchMessageA(&a);}
BOOL WINAPI x9_TranslateMessage_unused(const MSG*m){MSG a;msg_w2a(&a,m);return TranslateMessage(&a);}
BOOL WINAPI x9_IsDialogMessageW(HWND h,LPMSG m){MSG a;msg_w2a(&a,m);return IsDialogMessageA(h,&a);}
int WINAPI x9_TranslateAcceleratorW(HWND h,HACCEL t,LPMSG m){MSG a;msg_w2a(&a,m);return TranslateAcceleratorA(h,t,&a);}
static LRESULT WINAPI send_a(HWND h,UINT m,WPARAM w,LPARAM l){return SendMessageA(h,m,w,l);}
LRESULT WINAPI x9_SendMessageW(HWND h,UINT m,WPARAM w,LPARAM l){return call_a(send_a,0,h,m,w,l);}
LRESULT WINAPI x9_SendMessageTimeoutW(HWND h,UINT m,WPARAM w,LPARAM l,UINT f,UINT t,PDWORD_PTR r){
 if(is_char_msg(m))w=chr_w2a(w);
 if((m==WM_SETTEXT||m==WM_SETTINGCHANGE)&&l&&!is_res((void*)l)){LPSTR s=A((LPCWSTR)l);LRESULT x=SendMessageTimeoutA(h,m,w,(LPARAM)s,f,t,r);FREEA(s,(LPCWSTR)l);return x;}
 return SendMessageTimeoutA(h,m,w,l,f,t,r);}
BOOL WINAPI x9_PostMessageW(HWND h,UINT m,WPARAM w,LPARAM l){if(is_char_msg(m))w=chr_w2a(w);return PostMessageA(h,m,w,l);}
BOOL WINAPI x9_PostThreadMessageW(DWORD t,UINT m,WPARAM w,LPARAM l){if(is_char_msg(m))w=chr_w2a(w);return PostThreadMessageA(t,m,w,l);}
UINT WINAPI x9_RegisterWindowMessageW(LPCWSTR s){LPSTR a=A(s);UINT r=RegisterWindowMessageA(a);FREEA(a,s);return r;}
UINT WINAPI x9_RegisterClipboardFormatW(LPCWSTR s){LPSTR a=A(s);UINT r=RegisterClipboardFormatA(a);FREEA(a,s);return r;}
HANDLE WINAPI x9_GetPropW(HWND h,LPCWSTR s){LPSTR a=A(s);HANDLE r=GetPropA(h,a);FREEA(a,s);return r;}
BOOL WINAPI x9_SetPropW(HWND h,LPCWSTR s,HANDLE v){LPSTR a=A(s);BOOL r=SetPropA(h,a,v);FREEA(a,s);return r;}
HANDLE WINAPI x9_RemovePropW(HWND h,LPCWSTR s){LPSTR a=A(s);HANDLE r=RemovePropA(h,a);FREEA(a,s);return r;}
/* resources */
HCURSOR WINAPI x9_LoadCursorW(HINSTANCE h,LPCWSTR n){LPSTR a=A(n);HCURSOR r=LoadCursorA(h,a);FREEA(a,n);return r;}
HICON WINAPI x9_LoadIconW(HINSTANCE h,LPCWSTR n){LPSTR a=A(n);HICON r=LoadIconA(h,a);FREEA(a,n);return r;}
HANDLE WINAPI x9_LoadImageW(HINSTANCE h,LPCWSTR n,UINT t,int x,int y,UINT f){LPSTR a=A(n);HANDLE r=LoadImageA(h,a,t,x,y,f);FREEA(a,n);return r;}
HMENU WINAPI x9_LoadMenuW(HINSTANCE h,LPCWSTR n){LPSTR a=A(n);HMENU r=LoadMenuA(h,a);FREEA(a,n);return r;}
HMENU WINAPI x9_LoadMenuIndirectW(const MENUTEMPLATEW*t){return LoadMenuIndirectA((const MENUTEMPLATEA*)t);}
HACCEL WINAPI x9_LoadAcceleratorsW(HINSTANCE h,LPCWSTR n){LPSTR a=A(n);HACCEL r=LoadAcceleratorsA(h,a);FREEA(a,n);return r;}
/* LoadStringW: read the UTF-16 string table directly (identical on 9x). */
int WINAPI x9_LoadStringW(HINSTANCE h,UINT id,LPWSTR out,int cap){HRSRC r=FindResourceA(h,MAKEINTRESOURCEA((id>>4)+1),(LPCSTR)RT_STRING);HGLOBAL g;const WCHAR*p;UINT i,n;
 if(!r||!(g=LoadResource(h,r))||!(p=(const WCHAR*)LockResource(g)))return 0;
 for(i=0;i<(id&15);i++)p+=1+*p;n=*p++;
 if(cap==0){if(out)*(const WCHAR**)out=p;return n;}
 if(!out)return 0;if((int)n>=cap)n=cap-1;for(i=0;i<n;i++)out[i]=p[i];out[n]=0;return n;}
/* dialogs */
INT_PTR WINAPI x9_DialogBoxParamW(HINSTANCE h,LPCWSTR t,HWND p,DLGPROC d,LPARAM l){LPSTR a=A(t);INT_PTR r=DialogBoxParamA(h,a,p,(DLGPROC)thunk_of((WNDPROC)d,TRUE),l);FREEA(a,t);return r;}
INT_PTR WINAPI x9_DialogBoxIndirectParamW(HINSTANCE h,LPCDLGTEMPLATEW t,HWND p,DLGPROC d,LPARAM l){return DialogBoxIndirectParamA(h,t,p,(DLGPROC)thunk_of((WNDPROC)d,TRUE),l);}
HWND WINAPI x9_CreateDialogParamW(HINSTANCE h,LPCWSTR t,HWND p,DLGPROC d,LPARAM l){LPSTR a=A(t);HWND r=CreateDialogParamA(h,a,p,(DLGPROC)thunk_of((WNDPROC)d,TRUE),l);FREEA(a,t);return r;}
HWND WINAPI x9_CreateDialogIndirectParamW(HINSTANCE h,LPCDLGTEMPLATEW t,HWND p,DLGPROC d,LPARAM l){return CreateDialogIndirectParamA(h,t,p,(DLGPROC)thunk_of((WNDPROC)d,TRUE),l);}
int WINAPI x9_MessageBoxW(HWND h,LPCWSTR t,LPCWSTR c,UINT f){LPSTR a=A(t),b=A(c);int r=MessageBoxA(h,a,b,f);FREEA(a,t);FREEA(b,c);return r;}
/* menus */
BOOL WINAPI x9_InsertMenuW(HMENU m,UINT p,UINT f,UINT_PTR id,LPCWSTR s){LPSTR a=(f&(MF_BITMAP|MF_OWNERDRAW))?(LPSTR)s:A(s);BOOL r=InsertMenuA(m,p,f,id,a);if(!(f&(MF_BITMAP|MF_OWNERDRAW)))FREEA(a,s);return r;}
BOOL WINAPI x9_AppendMenuW(HMENU m,UINT f,UINT_PTR id,LPCWSTR s){LPSTR a=(f&(MF_BITMAP|MF_OWNERDRAW))?(LPSTR)s:A(s);BOOL r=AppendMenuA(m,f,id,a);if(!(f&(MF_BITMAP|MF_OWNERDRAW)))FREEA(a,s);return r;}
BOOL WINAPI x9_ModifyMenuW(HMENU m,UINT p,UINT f,UINT_PTR id,LPCWSTR s){LPSTR a=(f&(MF_BITMAP|MF_OWNERDRAW))?(LPSTR)s:A(s);BOOL r=ModifyMenuA(m,p,f,id,a);if(!(f&(MF_BITMAP|MF_OWNERDRAW)))FREEA(a,s);return r;}
int WINAPI x9_GetMenuStringW(HMENU m,UINT id,LPWSTR out,int cap,UINT f){char b[512];int n=GetMenuStringA(m,id,b,sizeof(b),f);if(!n){if(out&&cap)out[0]=0;return 0;}if(!out||!cap)return a2wlen(b);return a2wbuf(b,out,cap);}
typedef BOOL(WINAPI*MII)(HMENU,UINT,BOOL,LPMENUITEMINFOA);
static BOOL mii_call(const char*fn,HMENU m,UINT i,BOOL byp,LPMENUITEMINFOW w,BOOL get){MENUITEMINFOA a;MII f=(MII)GetProcAddress(GetModuleHandleA("USER32.DLL"),fn);BOOL r;char*buf=0;LPSTR s=0;
 if(!f||!w){SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
 a.cbSize=44;a.fMask=w->fMask;a.fType=w->fType;a.fState=w->fState;a.wID=w->wID;a.hSubMenu=w->hSubMenu;a.hbmpChecked=w->hbmpChecked;a.hbmpUnchecked=w->hbmpUnchecked;a.dwItemData=w->dwItemData;a.dwTypeData=0;a.cch=0;
 if((w->fMask&MIIM_TYPE)||(w->fMask&0x40/*MIIM_STRING*/)){if(get){if(w->dwTypeData&&w->cch){buf=(char*)mem(w->cch*2+2);a.dwTypeData=buf;a.cch=w->cch*2+1;}}
  else if(!(w->fType&(MFT_BITMAP|MFT_OWNERDRAW|MFT_SEPARATOR))){s=A(w->dwTypeData);a.dwTypeData=s;}else a.dwTypeData=(LPSTR)w->dwTypeData;}
 a.fMask&=~0x1E0; /* 9x knows only the original MIIM_* bits; string goes via MIIM_TYPE */
 if(w->fMask&0x40)a.fMask|=MIIM_TYPE;
 r=f(m,i,byp,&a);
 if(get&&r){w->fType=a.fType;w->fState=a.fState;w->wID=a.wID;w->hSubMenu=a.hSubMenu;w->hbmpChecked=a.hbmpChecked;w->hbmpUnchecked=a.hbmpUnchecked;w->dwItemData=a.dwItemData;
  if(buf){w->cch=a2wbuf(buf,w->dwTypeData,w->cch);}else w->cch=a.cch;}
 unmem(buf);if(s)FREEA(s,w->dwTypeData);return r;}
BOOL WINAPI x9_GetMenuItemInfoW(HMENU m,UINT i,BOOL p,LPMENUITEMINFOW w){return mii_call("GetMenuItemInfoA",m,i,p,w,TRUE);}
BOOL WINAPI x9_SetMenuItemInfoW(HMENU m,UINT i,BOOL p,LPCMENUITEMINFOW w){return mii_call("SetMenuItemInfoA",m,i,p,(LPMENUITEMINFOW)w,FALSE);}
BOOL WINAPI x9_InsertMenuItemW(HMENU m,UINT i,BOOL p,LPCMENUITEMINFOW w){return mii_call("InsertMenuItemA",m,i,p,(LPMENUITEMINFOW)w,FALSE);}
/* misc user32 */
BOOL WINAPI x9_SystemParametersInfoW(UINT a,UINT b,PVOID p,UINT f){lg("SPI %x %x %x",a,b,f);
 if(a==SPI_GETNONCLIENTMETRICS||a==SPI_GETICONTITLELOGFONT||a==SPI_GETDESKWALLPAPER){return FALSE;}
 return SystemParametersInfoA(a,b,p,f);}
UINT WINAPI x9_MapVirtualKeyW(UINT c,UINT t){return MapVirtualKeyA(c,t);}
LPWSTR WINAPI x9_CharUpperW(LPWSTR s){if(is_res(s)){WCHAR c=(WCHAR)(ULONG_PTR)s;if(c>='a'&&c<='z')c-=32;else if(c>=0xE0&&c<=0xFE&&c!=0xF7)c-=32;return (LPWSTR)(ULONG_PTR)c;}
 {int i;for(i=0;s[i];i++){WCHAR c=s[i];if(c>='a'&&c<='z')s[i]=c-32;else if(c>=0xE0&&c<=0xFE&&c!=0xF7)s[i]=c-32;}}return s;}
LPWSTR WINAPI x9_CharLowerW(LPWSTR s){if(is_res(s))return (LPWSTR)(ULONG_PTR)lowc((WCHAR)(ULONG_PTR)s);{int i;for(i=0;s[i];i++)s[i]=lowc(s[i]);}return s;}
DWORD WINAPI x9_CharUpperBuffW(LPWSTR s,DWORD n){DWORD i;for(i=0;i<n;i++){WCHAR c=s[i];if(c>='a'&&c<='z')s[i]=c-32;else if(c>=0xE0&&c<=0xFE&&c!=0xF7)s[i]=c-32;}return n;}
DWORD WINAPI x9_CharLowerBuffW(LPWSTR s,DWORD n){DWORD i;for(i=0;i<n;i++)s[i]=lowc(s[i]);return n;}
BOOL WINAPI x9_IsCharAlphaW(WCHAR c){return (ctype1(c)&C1_ALPHA)!=0;}
BOOL WINAPI x9_IsCharAlphaNumericW(WCHAR c){return (ctype1(c)&(C1_ALPHA|C1_DIGIT))!=0;}
/* wsprintfW: format through wvsprintfA with string arguments converted.
   Supports the conversions Flash uses (%s %S %d %u %x %X %c %ld %lu %lx %%). */
int WINAPIV x9_wsprintfW(LPWSTR out,LPCWSTR fmt,...){va_list ap;char buf[1100];char f2[8];int i=0,o=0,k;va_start(ap,fmt);
 while(fmt[i]&&o<1024){if(fmt[i]!='%'){char c[3];WCHAR w=fmt[i++];int n=WideCharToMultiByte(CP_ACP,0,&w,1,c,2,0,0);for(k=0;k<n&&o<1024;k++)buf[o++]=c[k];continue;}
  {int j=0;f2[j++]=fmt[i++];while(fmt[i]&&j<6&&(fmt[i]=='-'||fmt[i]=='0'||fmt[i]=='#'||(fmt[i]>='1'&&fmt[i]<='9')||fmt[i]=='l'||fmt[i]=='h'))f2[j++]=(char)fmt[i++];
   if(!fmt[i])break;f2[j++]=(char)fmt[i];f2[j]=0;
   switch(fmt[i++]){
    case 's':{LPCWSTR s=(f2[j-2]=='h')?0:va_arg(ap,LPCWSTR);LPSTR a=A(s?s:L"(null)");o+=wsprintfA(buf+o,"%s",a);FREEA(a,s);break;}
    case 'S':{LPCSTR s=va_arg(ap,LPCSTR);o+=wsprintfA(buf+o,"%s",s?s:"(null)");break;}
    case 'c':{WCHAR w=(WCHAR)va_arg(ap,int);char c[3];int n=WideCharToMultiByte(CP_ACP,0,&w,1,c,2,0,0);for(k=0;k<n;k++)buf[o++]=c[k];break;}
    case '%':buf[o++]='%';break;
    default:{int v=va_arg(ap,int);o+=wsprintfA(buf+o,f2,v);}}}}
 buf[o]=0;va_end(ap);return a2wbuf(buf,out,1025);}
/* DDE: Flash uses DDE only to forward a second launch to a running player. */
UINT WINAPI x9_DdeInitializeW(LPDWORD id,PFNCALLBACK cb,DWORD cmd,DWORD res){return DdeInitializeA(id,cb,cmd,res);}

/* ------------------------------------------------------------------ */
/* gdi32                                                               */
static void lf_w2a(const LOGFONTW*w,LOGFONTA*a){int i;char*d=(char*)a;const char*s=(const char*)w;for(i=0;i<28;i++)d[i]=s[i];w2abuf(w->lfFaceName,a->lfFaceName,LF_FACESIZE);}
static void lf_a2w(const LOGFONTA*a,LOGFONTW*w){int i;char*d=(char*)w;const char*s=(const char*)a;for(i=0;i<28;i++)d[i]=s[i];a2wbuf(a->lfFaceName,w->lfFaceName,LF_FACESIZE);}
static void tm_a2w(const TEXTMETRICA*a,LPTEXTMETRICW w){
 w->tmHeight=a->tmHeight;w->tmAscent=a->tmAscent;w->tmDescent=a->tmDescent;w->tmInternalLeading=a->tmInternalLeading;w->tmExternalLeading=a->tmExternalLeading;w->tmAveCharWidth=a->tmAveCharWidth;
 w->tmMaxCharWidth=a->tmMaxCharWidth;w->tmWeight=a->tmWeight;w->tmOverhang=a->tmOverhang;w->tmDigitizedAspectX=a->tmDigitizedAspectX;w->tmDigitizedAspectY=a->tmDigitizedAspectY;
 w->tmFirstChar=(BYTE)a->tmFirstChar;w->tmLastChar=(BYTE)a->tmLastChar>=0xFF?0xFFFF:(BYTE)a->tmLastChar;w->tmDefaultChar=(BYTE)a->tmDefaultChar;w->tmBreakChar=(BYTE)a->tmBreakChar;
 w->tmItalic=a->tmItalic;w->tmUnderlined=a->tmUnderlined;w->tmStruckOut=a->tmStruckOut;w->tmPitchAndFamily=a->tmPitchAndFamily;w->tmCharSet=a->tmCharSet;}
HFONT WINAPI x9_CreateFontIndirectW(const LOGFONTW*w){LOGFONTA a;lf_w2a(w,&a);return CreateFontIndirectA(&a);}
BOOL WINAPI x9_GetTextMetricsW(HDC h,LPTEXTMETRICW w){TEXTMETRICA a;if(!GetTextMetricsA(h,&a))return FALSE;tm_a2w(&a,w);return TRUE;}
int WINAPI x9_GetObjectW(HANDLE h,int n,LPVOID o){if(GetObjectType(h)==OBJ_FONT){LOGFONTA a;int r=GetObjectA(h,sizeof(a),&a);if(!r)return 0;if(!o)return sizeof(LOGFONTW);if(n<(int)sizeof(LOGFONTW)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}lf_a2w(&a,(LOGFONTW*)o);return sizeof(LOGFONTW);}
 return GetObjectA(h,n,o);}
typedef struct{FONTENUMPROCW p;LPARAM l;BOOL ex;}EnumCtx;
static int CALLBACK enum_thunk(const LOGFONTA*a,const TEXTMETRICA*t,DWORD type,LPARAM lp){EnumCtx*c=(EnumCtx*)lp;ENUMLOGFONTEXW e;NEWTEXTMETRICEXW n;int i;char*z;
 z=(char*)&e;for(i=0;i<(int)sizeof(e);i++)z[i]=0;z=(char*)&n;for(i=0;i<(int)sizeof(n);i++)z[i]=0;
 lf_a2w(a,&e.elfLogFont);
 if(c->ex){const ENUMLOGFONTEXA*ea=(const ENUMLOGFONTEXA*)a;a2wbuf((const char*)ea->elfFullName,e.elfFullName,LF_FULLFACESIZE);a2wbuf((const char*)ea->elfStyle,e.elfStyle,LF_FACESIZE);a2wbuf((const char*)ea->elfScript,e.elfScript,LF_FACESIZE);}
 else{const ENUMLOGFONTA*ea=(const ENUMLOGFONTA*)a;a2wbuf((const char*)ea->elfFullName,e.elfFullName,LF_FULLFACESIZE);a2wbuf((const char*)ea->elfStyle,e.elfStyle,LF_FACESIZE);}
 tm_a2w(t,(LPTEXTMETRICW)&n.ntmTm);
 if(type&TRUETYPE_FONTTYPE){const NEWTEXTMETRICA*na=(const NEWTEXTMETRICA*)t;n.ntmTm.ntmFlags=na->ntmFlags;n.ntmTm.ntmSizeEM=na->ntmSizeEM;n.ntmTm.ntmCellHeight=na->ntmCellHeight;n.ntmTm.ntmAvgWidth=na->ntmAvgWidth;
  if(c->ex)n.ntmFontSig=((const NEWTEXTMETRICEXA*)t)->ntmFontSig;}
 return c->p((const LOGFONTW*)&e,(const TEXTMETRICW*)&n,type,c->l);}
int WINAPI x9_EnumFontFamiliesW(HDC h,LPCWSTR face,FONTENUMPROCW p,LPARAM l){EnumCtx c;LPSTR a=A(face);int r;c.p=p;c.l=l;c.ex=FALSE;r=EnumFontFamiliesA(h,a,(FONTENUMPROCA)enum_thunk,(LPARAM)&c);FREEA(a,face);return r;}
int WINAPI x9_EnumFontFamiliesExW(HDC h,LPLOGFONTW lf,FONTENUMPROCW p,LPARAM l,DWORD f){EnumCtx c;LOGFONTA a;c.p=p;c.l=l;c.ex=TRUE;lf_w2a(lf,&a);return EnumFontFamiliesExA(h,&a,(FONTENUMPROCA)enum_thunk,(LPARAM)&c,f);}
int WINAPI x9_StartDocW(HDC h,const DOCINFOW*d){DOCINFOA a;LPSTR n=A(d->lpszDocName),o=A(d->lpszOutput);int r;a.cbSize=sizeof(a);a.lpszDocName=n;a.lpszOutput=o;a.lpszDatatype=0;a.fwType=0;r=StartDocA(h,&a);FREEA(n,d->lpszDocName);FREEA(o,d->lpszOutput);return r;}
HDC WINAPI x9_CreateDCW(LPCWSTR d,LPCWSTR dev,LPCWSTR o,const DEVMODEW*m){LPSTR a=A(d),b=A(dev),c=A(o);HDC r=CreateDCA(a,b,c,0);(void)m;FREEA(a,d);FREEA(b,dev);FREEA(c,o);return r;}

/* ------------------------------------------------------------------ */
/* shell32, version, winmm, comdlg32                                   */
UINT WINAPI x9_DragQueryFileW(HDROP d,UINT i,LPWSTR out,UINT cap){char b[MAX_PATH];UINT n;if(i==0xFFFFFFFF)return DragQueryFileA(d,i,0,0);n=DragQueryFileA(d,i,b,sizeof(b));if(!n)return 0;if(!out)return a2wlen(b);return a2wbuf(b,out,cap);}
BOOL WINAPI x9_ShellExecuteExW(LPSHELLEXECUTEINFOW w){SHELLEXECUTEINFOA a;LPSTR v=A(w->lpVerb),f=A(w->lpFile),p=A(w->lpParameters),d=A(w->lpDirectory);BOOL r;int i;char*z=(char*)&a;for(i=0;i<(int)sizeof(a);i++)z[i]=0;
 a.cbSize=sizeof(a);a.fMask=w->fMask&~SEE_MASK_UNICODE;a.hwnd=w->hwnd;a.lpVerb=v;a.lpFile=f;a.lpParameters=p;a.lpDirectory=d;a.nShow=w->nShow;r=ShellExecuteExA(&a);w->hInstApp=a.hInstApp;w->hProcess=a.hProcess;
 FREEA(v,w->lpVerb);FREEA(f,w->lpFile);FREEA(p,w->lpParameters);FREEA(d,w->lpDirectory);return r;}
HINSTANCE WINAPI x9_ShellExecuteW(HWND h,LPCWSTR v,LPCWSTR f,LPCWSTR p,LPCWSTR d,INT s){LPSTR a=A(v),b=A(f),c=A(p),e=A(d);HINSTANCE r=ShellExecuteA(h,a,b,c,e,s);FREEA(a,v);FREEA(b,f);FREEA(c,p);FREEA(e,d);return r;}
DWORD WINAPI x9_GetFileVersionInfoSizeW(LPCWSTR n,LPDWORD h){LPSTR a=A(n);DWORD r=GetFileVersionInfoSizeA(a,h);FREEA(a,n);return r;}
BOOL WINAPI x9_GetFileVersionInfoW(LPCWSTR n,DWORD h,DWORD l,LPVOID d){LPSTR a=A(n);BOOL r=GetFileVersionInfoA(a,h,l,d);FREEA(a,n);return r;}
/* VerQueryValueW: on 9x the version block is the 16-bit ANSI layout; query
   with the ANSI name and widen string values into a per-block cache. */
BOOL WINAPI x9_VerQueryValueW(LPCVOID blk,LPCWSTR sub,LPVOID*out,PUINT len){LPSTR a=A(sub);LPVOID p=0;UINT n=0;BOOL r=VerQueryValueA(blk,a,&p,&n);
 if(r&&a&&a[0]=='\\'&&(a[1]=='S'||a[1]=='s')&&p&&n){LPWSTR w=W((LPCSTR)p);*out=w;if(len)*len=wlen(w);}  /* small leak per query, as MSLU */
 else{*out=p;if(len)*len=n;}FREEA(a,sub);return r;}
MMRESULT WINAPI x9_waveOutGetDevCapsW(UINT_PTR d,LPWAVEOUTCAPSW w,UINT cb){WAVEOUTCAPSA a;MMRESULT r=waveOutGetDevCapsA(d,&a,sizeof(a));if(r)return r;(void)cb;
 w->wMid=a.wMid;w->wPid=a.wPid;w->vDriverVersion=a.vDriverVersion;a2wbuf(a.szPname,w->szPname,MAXPNAMELEN);w->dwFormats=a.dwFormats;w->wChannels=a.wChannels;w->wReserved1=a.wReserved1;w->dwSupport=a.dwSupport;return 0;}
MMRESULT WINAPI x9_waveInGetDevCapsW(UINT_PTR d,LPWAVEINCAPSW w,UINT cb){WAVEINCAPSA a;MMRESULT r=waveInGetDevCapsA(d,&a,sizeof(a));if(r)return r;(void)cb;
 w->wMid=a.wMid;w->wPid=a.wPid;w->vDriverVersion=a.vDriverVersion;a2wbuf(a.szPname,w->szPname,MAXPNAMELEN);w->dwFormats=a.dwFormats;w->wChannels=a.wChannels;w->wReserved1=a.wReserved1;return 0;}
/* GetOpenFileNameW / GetSaveFileNameW via the ANSI dialog. */
static BOOL ofn_call(LPOPENFILENAMEW w,BOOL save){OPENFILENAMEA a;char file[2048],title[MAX_PATH];LPSTR filt=0,cf=0,idir=0,tit=0,ext=0;BOOL r;int i,n;char*z=(char*)&a;
 for(i=0;i<(int)sizeof(a);i++)z[i]=0;
 if(w->lpstrFilter){LPCWSTR p=w->lpstrFilter;while(p[0]||p[1])p++;n=(int)(p-w->lpstrFilter)+2;filt=(LPSTR)mem(n*2+2);WideCharToMultiByte(CP_ACP,0,w->lpstrFilter,n,filt,n*2,0,0);}
 a.lStructSize=76;a.hwndOwner=w->hwndOwner;a.hInstance=w->hInstance;a.lpstrFilter=filt;a.nFilterIndex=w->nFilterIndex;
 file[0]=0;if(w->lpstrFile)w2abuf(w->lpstrFile,file,sizeof(file));a.lpstrFile=file;a.nMaxFile=sizeof(file);
 a.lpstrFileTitle=w->lpstrFileTitle?title:0;a.nMaxFileTitle=sizeof(title);idir=A(w->lpstrInitialDir);a.lpstrInitialDir=idir;tit=A(w->lpstrTitle);a.lpstrTitle=tit;ext=A(w->lpstrDefExt);a.lpstrDefExt=ext;
 a.Flags=w->Flags&~(OFN_ENABLEHOOK|OFN_ENABLETEMPLATE|OFN_ENABLETEMPLATEHANDLE|OFN_ALLOWMULTISELECT);
 r=save?GetSaveFileNameA(&a):GetOpenFileNameA(&a);
 if(r){if(w->lpstrFile)a2wbuf(file,w->lpstrFile,w->nMaxFile);if(w->lpstrFileTitle)a2wbuf(title,w->lpstrFileTitle,w->nMaxFileTitle);w->nFilterIndex=a.nFilterIndex;
  /* offsets in WCHARs */
  {char sv=file[a.nFileOffset];file[a.nFileOffset]=0;w->nFileOffset=(WORD)a2wlen(file);file[a.nFileOffset]=sv;}
  {char sv=file[a.nFileExtension];file[a.nFileExtension]=0;w->nFileExtension=(WORD)a2wlen(file);file[a.nFileExtension]=sv;}}
 unmem(filt);FREEA(idir,w->lpstrInitialDir);FREEA(tit,w->lpstrTitle);FREEA(ext,w->lpstrDefExt);(void)cf;return r;}
BOOL WINAPI x9_GetOpenFileNameW(LPOPENFILENAMEW w){return ofn_call(w,FALSE);}
BOOL WINAPI x9_GetSaveFileNameW(LPOPENFILENAMEW w){return ofn_call(w,TRUE);}
BOOL WINAPI x9_PrintDlgW(LPPRINTDLGW w){(void)w;return FALSE;}


/* ------------------------------------------------------------------ */
/* additions used by Flash Player 32                                   */
UINT WINAPI x9_GetDriveTypeW(LPCWSTR n){LPSTR a=A(n);UINT r=GetDriveTypeA(a);FREEA(a,n);return r;}
BOOL WINAPI x9_ReadConsoleW(HANDLE h,LPVOID buf,DWORD n,LPDWORD got,LPVOID c){char*b=(char*)mem(n+1);DWORD k=0;BOOL r=ReadConsoleA(h,b,n,&k,c);(void)c;
 if(r){k=MultiByteToWideChar(CP_ACP,0,b,k,(LPWSTR)buf,n);}if(got)*got=k;unmem(b);return r;}
HANDLE WINAPI x9_RegisterEventSourceW(LPCWSTR s,LPCWSTR n){(void)s;(void)n;return (HANDLE)0x39584556;}   /* no event log on 9x */
BOOL WINAPI x9_ReportEventW(HANDLE h,WORD t,WORD c,DWORD id,PSID u,WORD ns,DWORD sz,LPCWSTR*st,LPVOID d){(void)h;(void)t;(void)c;(void)id;(void)u;(void)ns;(void)sz;(void)st;(void)d;return TRUE;}
BOOL WINAPI x9_DeregisterEventSource(HANDLE h){(void)h;return TRUE;}
/* Windows 95 has no EnumDisplayDevices: report the single primary display. */
BOOL WINAPI x9_EnumDisplayDevicesW(LPCWSTR dev,DWORD i,PDISPLAY_DEVICEW d,DWORD fl){typedef BOOL(WINAPI*EDA)(LPCSTR,DWORD,DISPLAY_DEVICEA*,DWORD);EDA f=(EDA)GetProcAddress(GetModuleHandleA("USER32.DLL"),"EnumDisplayDevicesA");DISPLAY_DEVICEA a;LPSTR s;BOOL r;
 if(!d)return FALSE;
 if(!f){if(dev||i)return FALSE;x9_lstrcpyW(d->DeviceName,L"\\\\.\\DISPLAY1");x9_lstrcpyW(d->DeviceString,L"Display");d->StateFlags=DISPLAY_DEVICE_ATTACHED_TO_DESKTOP|DISPLAY_DEVICE_PRIMARY_DEVICE;d->DeviceID[0]=0;d->DeviceKey[0]=0;return TRUE;}
 a.cb=sizeof(a);s=A(dev);r=f(s,i,&a,fl);FREEA(s,dev);if(!r)return FALSE;
 a2wbuf(a.DeviceName,d->DeviceName,32);a2wbuf(a.DeviceString,d->DeviceString,128);d->StateFlags=a.StateFlags;a2wbuf(a.DeviceID,d->DeviceID,128);a2wbuf(a.DeviceKey,d->DeviceKey,128);return TRUE;}
BOOL WINAPI x9_EnumDisplayDevicesA(LPCSTR dev,DWORD i,PDISPLAY_DEVICEA d,DWORD fl){typedef BOOL(WINAPI*EDA)(LPCSTR,DWORD,DISPLAY_DEVICEA*,DWORD);EDA f=(EDA)GetProcAddress(GetModuleHandleA("USER32.DLL"),"EnumDisplayDevicesA");
 if(f)return f(dev,i,d,fl);if(!d||dev||i)return FALSE;lstrcpyA(d->DeviceName,"\\\\.\\DISPLAY1");lstrcpyA(d->DeviceString,"Display");d->StateFlags=DISPLAY_DEVICE_ATTACHED_TO_DESKTOP|DISPLAY_DEVICE_PRIMARY_DEVICE;d->DeviceID[0]=0;d->DeviceKey[0]=0;return TRUE;}


/* ntdll: Flash Player 32 looks up RtlInitUnicodeString dynamically and calls
   it without checking the result; the 9x NTDLL.DLL does not export it. */
typedef struct{USHORT Length,MaximumLength;PWSTR Buffer;}X9_USTR;
VOID WINAPI x9_RtlInitUnicodeString(X9_USTR*u,PCWSTR s){int n=wlen(s);if(!u)return;if(n>32766)n=32766;u->Buffer=(PWSTR)s;u->Length=s?(USHORT)(n*2):0;u->MaximumLength=s?(USHORT)(n*2+2):0;}

/* GetProcAddress: dynamic lookups of system W functions get the 9x layer. */
static BOOL system_handle(HMODULE m);
FARPROC WINAPI x9_GetProcAddress(HMODULE m,LPCSTR n){void*f;
 if(logf!=INVALID_HANDLE_VALUE){char p[MAX_PATH];FARPROC r;p[0]=0;GetModuleFileNameA(m,p,sizeof(p));r=GetProcAddress(m,n);lg("GPA %s %s -> %x",p,is_res(n)?"#ord":n,r);}
 if(!is_res(n)&&system_handle(m)){if((f=lookup_name(n))!=0)return (FARPROC)traced(n,f);if(alen(n)&&n[alen(n)-1]=='W')lg("unhandled GetProcAddress %s",n);}
 return GetProcAddress(m,n);}


/* Call tracing (only while logging): each patched import goes through a
   small stub that logs the first calls of that function. */
typedef struct{const char*name;void*fn;LONG count;}TraceEnt;
static void WINAPI trace_hit(TraceEnt*t){LONG c=InterlockedIncrement(&t->count);if(c<=300&&!(t->name[0]=='C'&&t->name[1]=='h')&&!(t->name[0]=='l'&&t->name[1]=='s')&&!(t->name[0]=='I'&&t->name[1]=='s'))lg("call %s",t->name);}
static BYTE*rwx(int n){BYTE*p;EnterCriticalSection(&tlock);if(tleft<n){tpool=(BYTE*)VirtualAlloc(0,65536,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);tleft=tpool?65536:0;}
 p=tpool;if(p){tpool+=n;tleft-=n;}LeaveCriticalSection(&tlock);return p;}
static void*traced(const char*name,void*fn){TraceEnt*t;BYTE*c;
 if(logf==INVALID_HANDLE_VALUE)return fn;
 t=(TraceEnt*)mem(sizeof(*t));t->name=name;t->fn=fn;c=rwx(20);if(!c)return fn;
 c[0]=0x60;c[1]=0x68;*(DWORD*)(c+2)=(DWORD)t;c[6]=0xE8;*(DWORD*)(c+7)=(DWORD)trace_hit-(DWORD)(c+11);c[11]=0x61;c[12]=0xE9;*(DWORD*)(c+13)=(DWORD)fn-(DWORD)(c+17);
 return c;}

/* ------------------------------------------------------------------ */
/* import patching (Windows 9x only)                                   */
typedef struct { const char*name; void*fn; } Entry;
extern const Entry x9_table[];
static void*lookup_name(const char*n){const Entry*e;for(e=x9_table;e->name;e++){const char*a=e->name,*b=n;while(*a&&*a==*b){a++;b++;}if(!*a&&!*b)return e->fn;}return 0;}
static BOOL system_module(const char*path){char sys[MAX_PATH],win[MAX_PATH];UINT n=GetSystemDirectoryA(sys,sizeof(sys)),k=GetWindowsDirectoryA(win,sizeof(win)),i;
 for(i=0;i<n&&path[i]&&(path[i]|32)==(sys[i]|32);i++){}if(i==n&&n)return TRUE;
 for(i=0;i<k&&path[i]&&(path[i]|32)==(win[i]|32);i++){}if(i==k&&k&&path[k]=='\\'){const char*p=path+k+1;while(*p&&*p!='\\')p++;if(!*p)return TRUE;}return FALSE;}
static void patch_module(HMODULE m){BYTE*b=(BYTE*)m;IMAGE_DOS_HEADER*d=(IMAGE_DOS_HEADER*)b;IMAGE_NT_HEADERS*nt;IMAGE_IMPORT_DESCRIPTOR*id;DWORD rva,sz;
 if(!m||m==self||d->e_magic!=IMAGE_DOS_SIGNATURE)return;nt=(IMAGE_NT_HEADERS*)(b+d->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return;
 rva=nt->OptionalHeader.DataDirectory[1].VirtualAddress;sz=nt->OptionalHeader.DataDirectory[1].Size;if(!rva||!sz)return;
 for(id=(IMAGE_IMPORT_DESCRIPTOR*)(b+rva);id->Name;id++){IMAGE_THUNK_DATA*names=(IMAGE_THUNK_DATA*)(b+(id->OriginalFirstThunk?id->OriginalFirstThunk:id->FirstThunk));IMAGE_THUNK_DATA*iat=(IMAGE_THUNK_DATA*)(b+id->FirstThunk);
  if(!id->OriginalFirstThunk)continue; /* names already overwritten: cannot match by name */
  {HMODULE t=GetModuleHandleA((const char*)(b+id->Name));if(!t||!system_handle(t))continue;} /* only imports of system DLLs; proxies keep their own code */
  for(;names->u1.AddressOfData;names++,iat++){void*f;DWORD old;
   if(names->u1.Ordinal&IMAGE_ORDINAL_FLAG32)continue;
   f=lookup_name((const char*)((IMAGE_IMPORT_BY_NAME*)(b+names->u1.AddressOfData))->Name);
   if(!f){const char*n=(const char*)((IMAGE_IMPORT_BY_NAME*)(b+names->u1.AddressOfData))->Name;int k=alen(n);if(k&&n[k-1]=='W')lg("unhandled import %x %s!%s",m,(char*)(b+id->Name),n);}
   if(!f||(void*)iat->u1.Function==f||!from_system((void*)iat->u1.Function))continue;
   lg("patch %x %s!%s",m,(char*)(b+id->Name),(const char*)((IMAGE_IMPORT_BY_NAME*)(b+names->u1.AddressOfData))->Name);
   f=traced((const char*)((IMAGE_IMPORT_BY_NAME*)(b+names->u1.AddressOfData))->Name,f);
   if(VirtualProtect(&iat->u1.Function,4,PAGE_READWRITE,&old)){iat->u1.Function=(DWORD)f;VirtualProtect(&iat->u1.Function,4,old,&old);}}}}
static BOOL system_handle(HMODULE m){char p[MAX_PATH];if(!m||!GetModuleFileNameA(m,p,sizeof(p)))return FALSE;return system_module(p);}
static BOOL from_system(void*p){MEMORY_BASIC_INFORMATION mb;if((DWORD)p>=0x80000000u)return TRUE;return VirtualQuery(p,&mb,sizeof(mb))&&mb.AllocationBase&&system_handle((HMODULE)mb.AllocationBase);}
static volatile LONG patching;
static void patch_all(void){HANDLE s;MODULEENTRY32 me;
 if(InterlockedExchange((LONG*)&patching,1))return;
 s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
 if(s!=INVALID_HANDLE_VALUE){me.dwSize=sizeof(me);if(Module32First(s,&me))do{if(!system_module(me.szExePath))patch_module(me.hModule);}while(Module32Next(s,&me));CloseHandle(s);}
 InterlockedExchange((LONG*)&patching,0);}
BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID x){(void)x;
 if(r==DLL_PROCESS_ATTACH){self=h;heap=GetProcessHeap();InitializeCriticalSection(&tlock);if(GetVersion()&0x80000000u)patch_all();}
 return TRUE;}
