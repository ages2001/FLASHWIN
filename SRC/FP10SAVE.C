/* FP10 private saved-game storage (derived from the FP32 save adapter).
   On systems without a native SHGetFolderPath, FPS.DLL provisions
   <player folder>\UserData\<user key> and publishes it in FP10_SAVEROOT.
   When that volume only supports 8.3 names (FAT on NT 3.1), FPS also sets
   FP10_USERDATA and this adapter maps long Flash names (Macromedia,
   #SharedObjects, *.sol, ...) to verified 8-character aliases recorded in
   FP10MAP.DAT. The mapping is confined to FP10_USERDATA; all other paths
   retain native Win32 behavior. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern void WINAPI FpObserve(const char*,DWORD);
extern LPCWSTR fp_LegacyFilePath(LPCWSTR);
extern HANDLE WINAPI fp_BaseCreateFileW(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
extern BOOL WINAPI fp_BaseGetFileAttributesExW(LPCWSTR,GET_FILEEX_INFO_LEVELS,LPVOID);
#define MAP_MAGIC 0x314d5046u
#define MAP_FILE L"\\FP10MAP.DAT"
#define LOCK_FILE L"\\FP10LOCK.LCK"
typedef struct {DWORD magic;WCHAR alias[13];WCHAR name[256];DWORD checksum;} Record;
typedef struct Search {struct Search*next;HANDLE handle;WCHAR parent[MAX_PATH],pattern[256];} Search;
static Search*searches;static volatile LONG search_lock;
static void lock_search(void){while(InterlockedExchange(&search_lock,1))Sleep(0);}
static void unlock_search(void){InterlockedExchange(&search_lock,0);}
static void zero(void*p,DWORD n){DWORD i;for(i=0;i<n;i++)((BYTE*)p)[i]=0;}
static DWORD hash(const void*p,DWORD n){DWORD i,h=2166136261u;for(i=0;i<n;i++){h^=((BYTE*)p)[i];h*=16777619u;}return h;}
static BOOL join(WCHAR*out,LPCWSTR a,LPCWSTR b){DWORD n=lstrlenW(a),k=lstrlenW(b);if(n+k>=MAX_PATH){SetLastError(ERROR_FILENAME_EXCED_RANGE);return FALSE;}lstrcpyW(out,a);lstrcpyW(out+n,b);return TRUE;}
static BOOL valid_record(Record*r){DWORD i;if(r->magic!=MAP_MAGIC||r->checksum!=hash(r,FIELD_OFFSET(Record,checksum)))return FALSE;for(i=0;i<13&&r->alias[i];i++){}if(i!=8)return FALSE;for(i=0;i<8;i++)if(!((r->alias[i]>='0'&&r->alias[i]<='9')||(r->alias[i]>='A'&&r->alias[i]<='F')))return FALSE;for(i=0;i<256&&r->name[i];i++){}return i>0&&i<256;}
static HANDLE map_lock(LPCWSTR parent){WCHAR path[MAX_PATH];HANDLE h;DWORD start=GetTickCount(),e;if(!join(path,parent,LOCK_FILE))return INVALID_HANDLE_VALUE;for(;;){h=CreateFileW(path,GENERIC_READ,0,0,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,0);if(h!=INVALID_HANDLE_VALUE)return h;e=GetLastError();if(e!=ERROR_SHARING_VIOLATION||GetTickCount()-start>5000){SetLastError(e);return INVALID_HANDLE_VALUE;}Sleep(1);}}
static int record_read(HANDLE h,Record*r){DWORD n;if(!ReadFile(h,r,sizeof(*r),&n,0))return -1;if(!n)return 0;if(n!=sizeof(*r)||!valid_record(r)){SetLastError(ERROR_INVALID_DATA);return -1;}return 1;}
/* The complete logical name is stored alongside each alias. A hash collision
   therefore selects another alias instead of silently sharing another save. */
static BOOL map_name(LPCWSTR parent,LPCWSTR name,BOOL create,BOOL reverse,LPWSTR out){WCHAR file[MAX_PATH],candidate[13],physical[MAX_PATH];HANDLE guard,h;Record row,newrow;DWORD e=0,seed,attempt,i,used,end;int status;BOOL found=FALSE,collision;
 if(!name[0]||lstrlenW(name)>255){SetLastError(ERROR_INVALID_NAME);return FALSE;}
 guard=map_lock(parent);if(guard==INVALID_HANDLE_VALUE)return FALSE;
 if(!join(file,parent,MAP_FILE)){e=GetLastError();goto done;}
 h=CreateFileW(file,GENERIC_READ|(create?GENERIC_WRITE:0),FILE_SHARE_READ,0,create?OPEN_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_HIDDEN,0);
 if(h==INVALID_HANDLE_VALUE){e=GetLastError();goto done;}
 while((status=record_read(h,&row))>0){
  if(!lstrcmpiW(reverse?row.alias:row.name,name)){lstrcpyW(out,reverse?row.name:row.alias);found=TRUE;goto close;}}
 if(status<0){e=GetLastError();goto close;}
 if(!create||reverse){e=ERROR_FILE_NOT_FOUND;goto close;}
 seed=hash(name,lstrlenW(name)*sizeof(WCHAR));
 for(attempt=0;attempt<65536;attempt++,seed+=0x9e3779b9u){
  for(i=0;i<8;i++)candidate[i]=L"0123456789ABCDEF"[(seed>>(28-4*i))&15];candidate[8]=0;collision=FALSE;
  SetFilePointer(h,0,0,FILE_BEGIN);
  while((status=record_read(h,&row))>0){if(!lstrcmpiW(row.alias,candidate)){collision=TRUE;break;}}
  if(status<0){e=GetLastError();goto close;}
  if(collision)continue;
  if(!join(physical,parent,L"\\")||!join(physical,physical,candidate)){e=GetLastError();goto close;}
  if(GetFileAttributesW(physical)!=INVALID_FILE_ATTRIBUTES)continue;
  if(GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND){e=GetLastError();goto close;}
  zero(&newrow,sizeof(newrow));newrow.magic=MAP_MAGIC;lstrcpyW(newrow.alias,candidate);lstrcpyW(newrow.name,name);newrow.checksum=hash(&newrow,FIELD_OFFSET(Record,checksum));
  end=SetFilePointer(h,0,0,FILE_END);if(end==INVALID_SET_FILE_POINTER){e=GetLastError();goto close;}if(!WriteFile(h,&newrow,sizeof(newrow),&used,0)||used!=sizeof(newrow)||!FlushFileBuffers(h)){e=GetLastError();if(!e)e=ERROR_WRITE_FAULT;SetFilePointer(h,end,0,FILE_BEGIN);SetEndOfFile(h);goto close;}
  lstrcpyW(out,candidate);found=TRUE;goto close;
 }
 e=ERROR_TOO_MANY_NAMES;
close:CloseHandle(h);
done:CloseHandle(guard);if(!found)SetLastError(e?e:ERROR_INVALID_DATA);return found;
}
/* Return 0 outside the namespace, 1 inside, -1 on invalid input. */
static int logical_path(LPCWSTR input,LPWSTR full,LPWSTR base){WCHAR prefix[MAX_PATH];DWORD n,k;input=fp_LegacyFilePath(input);if(!input){SetLastError(ERROR_INVALID_PARAMETER);return -1;}n=GetEnvironmentVariableW(L"FP10_USERDATA",base,MAX_PATH);if(!n)return 0;if(n>=MAX_PATH){SetLastError(ERROR_FILENAME_EXCED_RANGE);return -1;}k=GetFullPathNameW(input,MAX_PATH,full,0);if(!k||k>=MAX_PATH){if(k>=MAX_PATH)SetLastError(ERROR_FILENAME_EXCED_RANGE);return -1;}while(k>3&&full[k-1]=='\\')full[--k]=0;if(k<n)return 0;for(k=0;k<n;k++)prefix[k]=full[k];prefix[n]=0;if(lstrcmpiW(prefix,base)|| (full[n]&&full[n]!='\\'))return 0;return 1;}
static BOOL mapped(LPCWSTR input,LPWSTR out,BOOL create){WCHAR full[MAX_PATH],base[MAX_PATH],name[256],alias[13];DWORD i,k;int scope;input=fp_LegacyFilePath(input);scope=logical_path(input,full,base);if(scope<0)return FALSE;if(!scope){if(!input||lstrlenW(input)>=MAX_PATH){SetLastError(ERROR_FILENAME_EXCED_RANGE);return FALSE;}lstrcpyW(out,input);return TRUE;}lstrcpyW(out,base);i=lstrlenW(base);while(full[i]){if(full[i]=='\\')i++;k=0;while(full[i]&&full[i]!='\\'){if(k>=255){SetLastError(ERROR_FILENAME_EXCED_RANGE);return FALSE;}name[k++]=full[i++];}name[k]=0;if(!k)break;if(!map_name(out,name,create&&!full[i],FALSE,alias)){if(full[i]&&GetLastError()==ERROR_FILE_NOT_FOUND)SetLastError(ERROR_PATH_NOT_FOUND);return FALSE;}if(!join(out,out,L"\\")||!join(out,out,alias))return FALSE;}return TRUE;}
BOOL WINAPI fp_CreateDirectoryW(LPCWSTR p,LPSECURITY_ATTRIBUTES s){WCHAR path[MAX_PATH];return mapped(p,path,TRUE)&&CreateDirectoryW(path,s);}
BOOL WINAPI fp_RemoveDirectoryW(LPCWSTR p){WCHAR path[MAX_PATH],full[MAX_PATH],base[MAX_PATH],item[MAX_PATH];WIN32_FIND_DATAW data;HANDLE guard,h;DWORD error;int scope=logical_path(p,full,base);BOOL empty=TRUE;
 if(scope<0)return FALSE;if(!scope)return RemoveDirectoryW(p);if(!mapped(p,path,FALSE))return FALSE;
 guard=map_lock(path);if(guard==INVALID_HANDLE_VALUE)return FALSE;if(!join(item,path,L"\\*")){CloseHandle(guard);return FALSE;}
 h=FindFirstFileW(item,&data);if(h!=INVALID_HANDLE_VALUE){do{if(lstrcmpW(data.cFileName,L".")&&lstrcmpW(data.cFileName,L"..")&&lstrcmpiW(data.cFileName,L"FP10MAP.DAT")&&lstrcmpiW(data.cFileName,L"FP10LOCK.LCK")){empty=FALSE;break;}}while(FindNextFileW(h,&data));FindClose(h);}else if(GetLastError()!=ERROR_FILE_NOT_FOUND){error=GetLastError();CloseHandle(guard);SetLastError(error);return FALSE;}
 if(!empty){CloseHandle(guard);SetLastError(ERROR_DIR_NOT_EMPTY);return FALSE;}
 if(!join(item,path,MAP_FILE)){CloseHandle(guard);return FALSE;}if(!DeleteFileW(item)&&GetLastError()!=ERROR_FILE_NOT_FOUND){error=GetLastError();CloseHandle(guard);SetLastError(error);return FALSE;}
 CloseHandle(guard);if(!join(item,path,LOCK_FILE)||!DeleteFileW(item))return FALSE;return RemoveDirectoryW(path);
}
HANDLE WINAPI fp_CreateFileW(LPCWSTR p,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD how,DWORD flags,HANDLE template_file){WCHAR path[MAX_PATH];if(!mapped(p,path,how==CREATE_ALWAYS||how==CREATE_NEW||how==OPEN_ALWAYS))return INVALID_HANDLE_VALUE;{HANDLE h=fp_BaseCreateFileW(path,access,share,sa,how,flags,template_file);DWORD e=GetLastError();static DWORD observed;if(h==INVALID_HANDLE_VALUE&&observed++<32){char logical[MAX_PATH];DWORD j;for(j=0;j<MAX_PATH-1&&p[j];j++)logical[j]=p[j]<128?(char)p[j]:'?';logical[j]=0;FpObserve(logical,e);FpObserve("Save CreateFile access",access);FpObserve("Save CreateFile share",share);FpObserve("Save CreateFile how",how);FpObserve("Save CreateFile flags",flags);FpObserve("Save CreateFile caller",(DWORD)__builtin_return_address(0));}SetLastError(e);return h;}}
DWORD WINAPI fp_GetFileAttributesW(LPCWSTR p){WCHAR path[MAX_PATH];return mapped(p,path,FALSE)?GetFileAttributesW(path):INVALID_FILE_ATTRIBUTES;}
BOOL WINAPI fp_DeleteFileW(LPCWSTR p){WCHAR path[MAX_PATH];return mapped(p,path,FALSE)&&DeleteFileW(path);}
BOOL WINAPI fp_CopyFileW(LPCWSTR a,LPCWSTR b,BOOL fail_exists){WCHAR src[MAX_PATH],dst[MAX_PATH];return mapped(a,src,FALSE)&&mapped(b,dst,TRUE)&&CopyFileW(src,dst,fail_exists);}
BOOL WINAPI fp_MoveFileExW(LPCWSTR a,LPCWSTR b,DWORD flags){WCHAR src[MAX_PATH],dst[MAX_PATH];if(!mapped(a,src,FALSE))return FALSE;if(b&&!mapped(b,dst,TRUE))return FALSE;return MoveFileExW(src,b?dst:0,flags);}
/* ANSI entry points keep the native ANSI call unless the private 8.3 mapping
   is active, so Windows 9x (no wide file APIs) behaves exactly as before. */
static BOOL mapping_active(void){WCHAR b[4];return GetEnvironmentVariableW(L"FP10_USERDATA",b,4)!=0;}
static BOOL ansi(LPCSTR in,LPWSTR out){if(!in){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}return MultiByteToWideChar(CP_ACP,0,in,-1,out,MAX_PATH)!=0;}
HANDLE WINAPI fp_CreateFileA(LPCSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sa,DWORD c,DWORD f,HANDLE t){{if(!mapping_active())return CreateFileA(p,a,s,sa,c,f,t);}WCHAR w[MAX_PATH];if(!ansi(p,w))return INVALID_HANDLE_VALUE;return fp_CreateFileW(w,a,s,sa,c,f,t);}
DWORD WINAPI fp_GetFileAttributesA(LPCSTR p){{if(!mapping_active())return GetFileAttributesA(p);}WCHAR w[MAX_PATH];return ansi(p,w)?fp_GetFileAttributesW(w):INVALID_FILE_ATTRIBUTES;}
BOOL WINAPI fp_DeleteFileA(LPCSTR p){{if(!mapping_active())return DeleteFileA(p);}WCHAR w[MAX_PATH];return ansi(p,w)&&fp_DeleteFileW(w);}
BOOL WINAPI fp_CreateDirectoryA(LPCSTR p,LPSECURITY_ATTRIBUTES sa){{if(!mapping_active())return CreateDirectoryA(p,sa);}WCHAR w[MAX_PATH];return ansi(p,w)&&fp_CreateDirectoryW(w,sa);}
static WCHAR upper(WCHAR c){return (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)c);}
static BOOL matches(LPCWSTR pattern,LPCWSTR name){LPCWSTR star=0,retry=0;if(!lstrcmpW(pattern,L"*.*"))return TRUE;while(*name){if(*pattern=='*'){star=++pattern;retry=name;}else if(*pattern=='?'||upper(*pattern)==upper(*name)){pattern++;name++;}else if(star){pattern=star;name=++retry;}else return FALSE;}while(*pattern=='*')pattern++;return !*pattern;}
/* 1 = match, 0 = skip, -1 = metadata error. Never hide a corrupt map. */
/* "." and ".." are reported like a native directory listing: Flash treats an
   empty enumeration of an existing folder as an error and retries forever. */
static int translate_found(Search*s,LPWIN32_FIND_DATAW data){WCHAR name[256];if(!lstrcmpW(data->cFileName,L".")||!lstrcmpW(data->cFileName,L".."))return matches(s->pattern,data->cFileName)?1:0;if(!lstrcmpiW(data->cFileName,L"FP10MAP.DAT")||!lstrcmpiW(data->cFileName,L"FP10LOCK.LCK"))return 0;if(!map_name(s->parent,data->cFileName,FALSE,TRUE,name)){if(GetLastError()==ERROR_FILE_NOT_FOUND)SetLastError(ERROR_INVALID_DATA);return -1;}if(!matches(s->pattern,name))return 0;lstrcpyW(data->cAlternateFileName,data->cFileName);lstrcpyW(data->cFileName,name);return 1;}
static LPCWSTR search_input(LPCWSTR input,LPWSTR scratch){DWORD i,n;LPCWSTR parent;if(!input)return input;n=lstrlenW(input);if(n>=MAX_PATH)return input;i=n;while(i&&input[i]!='\\')i--;if(!i)return fp_LegacyFilePath(input);lstrcpyW(scratch,input);scratch[i]=0;parent=fp_LegacyFilePath(scratch);if(parent!=scratch){DWORD j,k=lstrlenW(parent);for(j=0;j<=k;j++)scratch[j]=parent[j];if(k+n-i>=MAX_PATH)return input;lstrcpyW(scratch+k,input+i);return scratch;}return fp_LegacyFilePath(input);}
static HANDLE find_first(LPCWSTR input,LPWIN32_FIND_DATAW data);
HANDLE WINAPI fp_FindFirstFileW(LPCWSTR input,LPWIN32_FIND_DATAW data){return find_first(input,data);}
static HANDLE find_first(LPCWSTR input,LPWIN32_FIND_DATAW data){WCHAR full[MAX_PATH],base[MAX_PATH],physical[MAX_PATH],search[MAX_PATH],scratch[MAX_PATH];DWORD i,n,error;Search*s;HANDLE h;int scope,status;input=search_input(input,scratch);scope=logical_path(input,full,base);if(scope<0)return INVALID_HANDLE_VALUE;if(!scope||!full[lstrlenW(base)])return FindFirstFileW(input,data);if(!data){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}n=lstrlenW(full);i=n;while(i&&full[i]!='\\')i--;if(!i||n-i>255){SetLastError(ERROR_INVALID_NAME);return INVALID_HANDLE_VALUE;}s=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*s));if(!s){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return INVALID_HANDLE_VALUE;}lstrcpyW(s->pattern,full+i+1);full[i]=0;if(!mapped(full,physical,FALSE)||!join(search,physical,L"\\*")){error=GetLastError();HeapFree(GetProcessHeap(),0,s);SetLastError(error);return INVALID_HANDLE_VALUE;}lstrcpyW(s->parent,physical);h=FindFirstFileW(search,data);if(h==INVALID_HANDLE_VALUE){error=GetLastError();HeapFree(GetProcessHeap(),0,s);SetLastError(error);return h;}s->handle=h;for(;;){status=translate_found(s,data);if(status>0){lock_search();s->next=searches;searches=s;unlock_search();return h;}if(status<0){error=GetLastError();break;}if(!FindNextFileW(h,data)){error=GetLastError();if(error==ERROR_NO_MORE_FILES)error=ERROR_FILE_NOT_FOUND;break;}}FindClose(h);HeapFree(GetProcessHeap(),0,s);SetLastError(error);return INVALID_HANDLE_VALUE;}
BOOL WINAPI fp_FindNextFileW(HANDLE h,LPWIN32_FIND_DATAW data){Search*s;BOOL result;int status;DWORD error;lock_search();for(s=searches;s&&s->handle!=h;s=s->next){}if(!s){unlock_search();return FindNextFileW(h,data);}if(!data){unlock_search();SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}for(;;){result=FindNextFileW(h,data);if(!result)break;status=translate_found(s,data);if(status>0)break;if(status<0){result=FALSE;break;}}error=GetLastError();unlock_search();SetLastError(error);return result;}
BOOL WINAPI fp_FindClose(HANDLE h){Search**p,*s=0;BOOL result;lock_search();for(p=&searches;*p;p=&(*p)->next)if((*p)->handle==h){s=*p;*p=s->next;break;}result=FindClose(h);unlock_search();if(s)HeapFree(GetProcessHeap(),0,s);return result;}
BOOL WINAPI fp_GetFileAttributesExW(LPCWSTR p,GET_FILEEX_INFO_LEVELS level,LPVOID out){WCHAR path[MAX_PATH];if(!mapped(p,path,FALSE))return FALSE;return fp_BaseGetFileAttributesExW(path,level,out);}

BOOL WINAPI fp_CopyFileA(LPCSTR a,LPCSTR b,BOOL fail_exists){{if(!mapping_active())return CopyFileA(a,b,fail_exists);}WCHAR wa[MAX_PATH],wb[MAX_PATH];return ansi(a,wa)&&ansi(b,wb)&&fp_CopyFileW(wa,wb,fail_exists);}
BOOL WINAPI fp_SetFileAttributesA(LPCSTR p,DWORD attributes){{if(!mapping_active())return SetFileAttributesA(p,attributes);}WCHAR w[MAX_PATH],path[MAX_PATH];if(!ansi(p,w))return FALSE;return mapped(w,path,FALSE)&&SetFileAttributesW(path,attributes);}
/* Fallback random source for systems without CryptoAPI (NT 3.x). Flash
   only needs it here to name its private SharedObject folder; without it
   SharedObjects are disabled. Seeded from timers, ids and the clock and
   re-mixed with the performance counter on every call (splitmix64). This is
   NOT a cryptographic generator; a native CryptoAPI is always preferred. */
static volatile LONG rng_lock;static ULONGLONG rng_state;static int rng_seeded;
static ULONGLONG mix64(ULONGLONG z){z^=z>>30;z*=0xbf58476d1ce4e5b9ull;z^=z>>27;z*=0x94d049bb133111ebull;return z^(z>>31);}
BOOL WINAPI fp_CryptAcquireContextA(ULONG_PTR*prov,LPCSTR container,LPCSTR provider,DWORD type,DWORD flags){(void)container;(void)provider;(void)type;(void)flags;if(!prov){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}*prov=0x46503130;return TRUE;}
BOOL WINAPI fp_CryptReleaseContext(ULONG_PTR prov,DWORD flags){(void)flags;if(prov!=0x46503130){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}return TRUE;}
BOOL WINAPI fp_CryptGenRandom(ULONG_PTR prov,DWORD n,BYTE*out){LARGE_INTEGER q;FILETIME ft;DWORD i;ULONGLONG v=0;
 if(prov!=0x46503130||(!out&&n)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 while(InterlockedExchange(&rng_lock,1))Sleep(0);
 if(!rng_seeded){SYSTEMTIME st;GetSystemTime(&st);SystemTimeToFileTime(&st,&ft);rng_state=((ULONGLONG)ft.dwHighDateTime<<32)^ft.dwLowDateTime;rng_state^=(ULONGLONG)GetCurrentProcessId()<<40^(ULONGLONG)GetCurrentThreadId()<<20^GetTickCount();rng_state^=(ULONGLONG)(ULONG_PTR)&q;rng_seeded=1;}
 for(i=0;i<n;i++){if(!(i&7)){if(QueryPerformanceCounter(&q))rng_state^=mix64((ULONGLONG)q.QuadPart);rng_state+=0x9e3779b97f4a7c15ull;v=mix64(rng_state);}out[i]=(BYTE)v;v>>=8;}
 InterlockedExchange(&rng_lock,0);return TRUE;}
extern DWORD WINAPI fp_GetUserDefaultUILanguage(void);
/* Flash resolves several APIs with GetProcAddress instead of importing them:
   kernel32!CreateDirectoryW (SharedObject folders: must go through the 8.3
   adapter above), kernel32!GetUserDefaultUILanguage, the advapi32 CryptoAPI
   random functions and shell32!SHGetFolderPathW. Route those to the
   compatibility versions; CreateDirectoryW always, the others only when the
   OS does not export them. */
static int same_module(HMODULE m,const char*name){HMODULE h=GetModuleHandleA(name);return h&&h==m;}
/* Flash also resolves shell32!SHGetFolderPathW dynamically for its
   SharedObject root. Where the OS lacks it (NT 3.x, NT 4 / 95 without
   SHFOLDER), hand out the FPS.DLL bridge so the private save folder is used
   instead of disabling SharedObjects entirely. */
FARPROC WINAPI fp_GetProcAddress(HMODULE module,LPCSTR name){
 FARPROC f;DWORD e;HMODULE s;
 if(name&&((ULONG_PTR)name>>16)&&!lstrcmpA(name,"CreateDirectoryW")&&same_module(module,"KERNEL32.DLL"))return (FARPROC)fp_CreateDirectoryW;
 f=GetProcAddress(module,name);
 if(f||!name||!((ULONG_PTR)name>>16))return f;
 e=GetLastError();
 if(same_module(module,"KERNEL32.DLL")&&!lstrcmpA(name,"GetUserDefaultUILanguage"))return (FARPROC)fp_GetUserDefaultUILanguage;
 if(same_module(module,"ADVAPI32.DLL")){
  if(!lstrcmpA(name,"CryptAcquireContextA"))return (FARPROC)fp_CryptAcquireContextA;
  if(!lstrcmpA(name,"CryptGenRandom"))return (FARPROC)fp_CryptGenRandom;
  if(!lstrcmpA(name,"CryptReleaseContext"))return (FARPROC)fp_CryptReleaseContext;}
 SetLastError(e);
 if(lstrcmpA(name,"SHGetFolderPathW")&&lstrcmpA(name,"SHGetFolderPathA"))return f;
 e=GetLastError();s=GetModuleHandleA("FPS.DLL");
 if(s&&s!=module){f=GetProcAddress(s,name);if(f)return f;}
 SetLastError(e);return 0;
}
