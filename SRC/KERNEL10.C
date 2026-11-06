#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef FP_DYNAMIC_CPU
#include "fp10_cpu_mode.h"
#endif
#if defined(FP_QUERY_XMM) || defined(FP_DYNAMIC_CPU)
#include "cpu_features.h"
#endif
void WINAPI fp_GetSystemTimeAsFileTime(LPFILETIME out){SYSTEMTIME s;GetSystemTime(&s);SystemTimeToFileTime(&s,out);}
BOOL WINAPI fp_GetFileSizeEx(HANDLE h,PLARGE_INTEGER out){DWORD hi,lo;if(!out){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}SetLastError(0);lo=GetFileSize(h,&hi);if(lo==0xffffffff && GetLastError())return FALSE;out->LowPart=lo;out->HighPart=(LONG)hi;return TRUE;}
BOOL WINAPI fp_SetFilePointerEx(HANDLE h,LARGE_INTEGER off,PLARGE_INTEGER result,DWORD method){LONG hi=off.HighPart;DWORD lo;SetLastError(0);lo=SetFilePointer(h,off.LowPart,&hi,method);if(lo==0xffffffff && GetLastError())return FALSE;if(result){result->LowPart=lo;result->HighPart=hi;}return TRUE;}
LONG WINAPI fp_InterlockedCompareExchange(volatile LONG *p,LONG value,LONG comparand){LONG old;__asm__ volatile("lock cmpxchgl %2,%1":"=a"(old),"+m"(*p):"r"(value),"0"(comparand):"memory","cc");return old;}
LONG WINAPI fp_InterlockedExchangeAdd(volatile LONG *p,LONG value){__asm__ volatile("lock xaddl %0,%1":"+r"(value),"+m"(*p)::"memory","cc");return value;}
BOOL WINAPI fp_InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION p,DWORD spin){(void)spin;InitializeCriticalSection(p);return TRUE;}
BOOL WINAPI fp_SwitchToThread(void){/* NtYieldExecution isn't exported on all early builds. Sleep(0) performs the yield. */Sleep(0);return FALSE;}
BOOL WINAPI fp_IsDebuggerPresent(void){BYTE *peb;__asm__ volatile("movl %%fs:0x30,%0":"=r"(peb));return peb[2]!=0;}
BOOL WINAPI fp_IsProcessorFeaturePresent(DWORD feature){DWORD a=1,b,c,d;
#ifdef FP_DYNAMIC_CPU
if(feature==PF_XMMI_INSTRUCTIONS_AVAILABLE || feature==PF_XMMI64_INSTRUCTIONS_AVAILABLE)return fp10_selected_sse2();
#endif
#ifdef FP_QUERY_XMM
if(feature==PF_XMMI_INSTRUCTIONS_AVAILABLE || feature==PF_XMMI64_INSTRUCTIONS_AVAILABLE)return fp_sse_feature(feature);
d=fp_cpu_edx();
#elif defined(FP_DYNAMIC_CPU)
d=fp_cpu_edx();
#else
__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
#endif
if(feature==PF_MMX_INSTRUCTIONS_AVAILABLE)return (d&(1u<<23))!=0;if(feature==PF_COMPARE_EXCHANGE_DOUBLE)return (d&(1u<<8))!=0;if(feature==PF_FLOATING_POINT_EMULATED)return !(d&1);return FALSE;}
LANGID WINAPI fp_GetUserDefaultUILanguage(void){return GetUserDefaultLangID();}
#include "version_compat.h"
static LPCH native_environment(void){typedef LPCH(WINAPI *F)(void);F f=(F)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"GetEnvironmentStrings");return f?f():0;}
LPWCH WINAPI fp_GetEnvironmentStringsW(void){LPCH a=native_environment();LPWCH w;DWORD n=0;int count;if(!a)return 0;while(a[n]||a[n+1])n++;n+=2;count=MultiByteToWideChar(CP_ACP,0,a,n,0,0);if(!count)return 0;w=HeapAlloc(GetProcessHeap(),0,count*sizeof(WCHAR));if(!w){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}if(!MultiByteToWideChar(CP_ACP,0,a,n,w,count)){HeapFree(GetProcessHeap(),0,w);return 0;}return w;}
BOOL WINAPI fp_FreeEnvironmentStringsW(LPWCH p){if(!p){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}return HeapFree(GetProcessHeap(),0,p);}
BOOL WINAPI fp_FreeEnvironmentStringsA(LPCH p){if(p==native_environment()&&p)return TRUE;SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
BOOL WINAPI fp_BaseGetFileAttributesExW(LPCWSTR path,GET_FILEEX_INFO_LEVELS level,LPVOID result){WIN32_FILE_ATTRIBUTE_DATA*out=result;BY_HANDLE_FILE_INFORMATION info;WIN32_FIND_DATAW fd;HANDLE h;LPCWSTR c;if(!path||!result||level!=GetFileExInfoStandard){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}for(c=path;*c;c++)if(*c=='*'||(*c=='?'&&c!=path+2)){SetLastError(ERROR_INVALID_NAME);return FALSE;}h=CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,0);if(h!=INVALID_HANDLE_VALUE){BOOL ok=GetFileInformationByHandle(h,&info);DWORD err=GetLastError();CloseHandle(h);if(ok){out->dwFileAttributes=info.dwFileAttributes;out->ftCreationTime=info.ftCreationTime;out->ftLastAccessTime=info.ftLastAccessTime;out->ftLastWriteTime=info.ftLastWriteTime;out->nFileSizeHigh=info.nFileSizeHigh;out->nFileSizeLow=info.nFileSizeLow;return TRUE;}SetLastError(err);}h=FindFirstFileW(path,&fd);if(h==INVALID_HANDLE_VALUE)return FALSE;FindClose(h);out->dwFileAttributes=fd.dwFileAttributes;out->ftCreationTime=fd.ftCreationTime;out->ftLastAccessTime=fd.ftLastAccessTime;out->ftLastWriteTime=fd.ftLastWriteTime;out->nFileSizeHigh=fd.nFileSizeHigh;out->nFileSizeLow=fd.nFileSizeLow;return TRUE;}
ULONGLONG WINAPI fp_VerSetConditionMask(ULONGLONG mask,DWORD types,BYTE cond){unsigned i;for(i=0;i<8;i++)if(types&(1u<<i)){mask&=~((ULONGLONG)7<<(i*3));mask|=(ULONGLONG)(cond&7)<<(i*3);}return mask;}
static int version_cmp(DWORD a,DWORD b,BYTE op){switch(op){case VER_EQUAL:return a==b;case VER_GREATER:return a>b;case VER_GREATER_EQUAL:return a>=b;case VER_LESS:return a<b;case VER_LESS_EQUAL:return a<=b;default:return -1;}}
BOOL WINAPI fp_VerifyVersionInfoW(LPOSVERSIONINFOEXW want,DWORD types,ULONGLONG mask){OSVERSIONINFOEXW have;DWORD a[4],b[4],ids[4]={VER_MAJORVERSION,VER_MINORVERSION,VER_SERVICEPACKMAJOR,VER_SERVICEPACKMINOR};unsigned shifts[4]={3,0,15,12};int i,last=-1,ok;BYTE op;if(!want||want->dwOSVersionInfoSize!=sizeof(*want)||!types||(types&~255u)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}have.dwOSVersionInfoSize=sizeof(have);if(!fp_GetVersionExW((LPOSVERSIONINFOW)&have))return FALSE;a[0]=have.dwMajorVersion;a[1]=have.dwMinorVersion;a[2]=have.wServicePackMajor;a[3]=have.wServicePackMinor;b[0]=want->dwMajorVersion;b[1]=want->dwMinorVersion;b[2]=want->wServicePackMajor;b[3]=want->wServicePackMinor;for(i=0;i<4;i++)if(types&ids[i])last=i;for(i=0;i<4;i++)if(types&ids[i]){op=(BYTE)((mask>>shifts[i])&7);ok=version_cmp(a[i],b[i],op);if(ok<0)goto invalid;if(a[i]!=b[i]||i==last){if(!ok)goto old;break;}}if(types&VER_BUILDNUMBER){ok=version_cmp(have.dwBuildNumber,want->dwBuildNumber,(BYTE)((mask>>6)&7));if(ok<0)goto invalid;if(!ok)goto old;}if(types&VER_PLATFORMID){ok=version_cmp(have.dwPlatformId,want->dwPlatformId,(BYTE)((mask>>9)&7));if(ok<0)goto invalid;if(!ok)goto old;}if(types&VER_PRODUCT_TYPE){ok=version_cmp(have.wProductType,want->wProductType,(BYTE)((mask>>21)&7));if(ok<0)goto invalid;if(!ok)goto old;}if(types&VER_SUITENAME){op=(BYTE)((mask>>18)&7);if(op==VER_AND){if((have.wSuiteMask&want->wSuiteMask)!=want->wSuiteMask)goto old;}else if(op==VER_OR){if(!(have.wSuiteMask&want->wSuiteMask))goto old;}else goto invalid;}return TRUE;invalid:SetLastError(ERROR_INVALID_PARAMETER);return FALSE;old:SetLastError(ERROR_OLD_WIN_VERSION);return FALSE;}

extern void WINAPI FpObserve(const char*,DWORD);
extern void WINAPI FpReport(const char*,DWORD);
/* NT 3.1 does not recognize the later Win32 extended-path prefix.
   Only translate ordinary absolute DOS paths whose spelling is unchanged by
   DOS normalization. Other extended paths retain the native error. */
static LPCWSTR legacy_file_path(LPCWSTR name){
 int i,start,n=0;if(!name)return name;while(name[n]&&n<260)n++;
 if(n>=260||n<7||name[0]!=92||name[1]!=92||name[2]!='?'||name[3]!=92||name[5]!=':'||name[6]!=92)return name;
 if(!((name[4]>='A'&&name[4]<='Z')||(name[4]>='a'&&name[4]<='z')))return name;
 start=7;for(i=7;i<=n;i++){
  if(name[i]=='/'||name[i]==':'||name[i]=='*'||name[i]=='?')return name;
  if(!name[i]||name[i]==92){int len=i-start;
   if(len==0||name[i-1]=='.'||name[i-1]==' ')return name;
   /* Avoid translating device names into DOS devices. */
   if(len>=3){WCHAR a=name[start]&~32,b=name[start+1]&~32,c=name[start+2]&~32;
    if((a=='C'&&b=='O'&&c=='N')||(a=='P'&&b=='R'&&c=='N')||(a=='A'&&b=='U'&&c=='X')||(a=='N'&&b=='U'&&c=='L')||(a=='C'&&b=='O'&&c=='M')||(a=='L'&&b=='P'&&c=='T'))return name;}
   start=i+1;
  }
 }
 return name+4;
}
/* Shared with the private save-name adapter (fp10_save.c). */
LPCWSTR fp_LegacyFilePath(LPCWSTR name){return legacy_file_path(name);}
HANDLE WINAPI fp_BaseCreateFileW(LPCWSTR name,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES sa,DWORD creation,DWORD attrs,HANDLE templ){HANDLE h=CreateFileW(legacy_file_path(name),access,sharing,sa,creation,attrs,templ);DWORD e=GetLastError();char path[256];int i;for(i=0;i<255&&name[i];i++)path[i]=name[i]<128?(char)name[i]:'?';path[i]=0;FpObserve(path,(DWORD)h);if(h==INVALID_HANDLE_VALUE)FpObserve("CreateFileW error",e);SetLastError(e);return h;}
static LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;
static LONG WINAPI diagnostic_exception(EXCEPTION_POINTERS*p){FpReport("Unhandled exception code",p->ExceptionRecord->ExceptionCode);FpReport("Unhandled exception address",(DWORD)p->ExceptionRecord->ExceptionAddress);FpReport("Exception EIP",p->ContextRecord->Eip);FpReport("Exception ESP",p->ContextRecord->Esp);return previous_filter?previous_filter(p):EXCEPTION_CONTINUE_SEARCH;}
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI fp_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter){LPTOP_LEVEL_EXCEPTION_FILTER old=previous_filter;previous_filter=filter;SetUnhandledExceptionFilter(diagnostic_exception);return old;}
void WINAPI fp_ExitProcess(UINT code){FpObserve("ExitProcess code",code);ExitProcess(code);}
BOOL WINAPI fp_TerminateProcess(HANDLE h,UINT code){FpObserve("TerminateProcess code",code);FpObserve("TerminateProcess caller",(DWORD)__builtin_return_address(0));return TerminateProcess(h,code);}
void WINAPI fp_ExitThread(DWORD code){FpObserve("ExitThread code",code);ExitThread(code);}
HRSRC WINAPI fp_FindResourceExW(HMODULE m,LPCWSTR type,LPCWSTR name,WORD lang){HRSRC r=FindResourceExW(m,type,name,lang);if(!r&&lang!=1033)r=FindResourceExW(m,type,name,1033);DWORD e=GetLastError();FpObserve("FindResourceExW type",(DWORD)type);FpObserve("FindResourceExW name",(DWORD)name);FpObserve("FindResourceExW lang",lang);FpObserve("FindResourceExW result",(DWORD)r);if(!r)FpObserve("FindResourceExW error",e);SetLastError(e);return r;}

HRSRC WINAPI fp_FindResourceExA(HMODULE m,LPCSTR type,LPCSTR name,WORD lang){HRSRC r=FindResourceExA(m,type,name,lang);if(!r&&lang!=1033)r=FindResourceExA(m,type,name,1033);DWORD e=GetLastError();FpObserve("FindResourceExA type",(DWORD)type);FpObserve("FindResourceExA name",(DWORD)name);FpObserve("FindResourceExA lang",lang);FpObserve("FindResourceExA result",(DWORD)r);if(!r)FpObserve("FindResourceExA error",e);SetLastError(e);return r;}
