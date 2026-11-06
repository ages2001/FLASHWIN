#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* Single-file portable launcher. The player EXE and every compatibility proxy
   DLL are packed into this one EXE (FKP1 trailer, same format as the installer).
   On run it extracts them to a fresh scratch folder, launches the player there
   (forwarding a .swf argument), waits, then deletes the folder. Nothing is
   installed and no registry entry is made. Notes that matter on old Windows:
   the scratch folder name is 8.3-safe (RFP + 5 hex) so it is valid on NT 3.1's
   plain FAT (no VFAT); and the player is launched with an explicit application
   name, because NT 3.1's CreateProcess fails a NULL app name + quoted command
   line. No CRT. RUN_EXE is the player to start; a packed name may hold one
   backslash (APP\FP32.EXE).
   Saved games: the scratch folder is deleted on exit, so SharedObjects must
   not live there. FP10 gets FP10_USERDATA_ROOT=<portable folder>\FP10DATA
   (FPS.DLL then uses <that>\UserData\<user> when the OS has no native
   SHGetFolderPath). FP32 gets FP32_USERDATA=<portable folder>\FP32DATA\<user>
   under the same condition, as its installed launcher would set it. On
   systems with a native AppData folder both players keep using it. */
#ifndef RUN_EXE
#define RUN_EXE "FP10.EXE"
#endif
#define FKP_MAGIC 0x31504b46u
#define MAXF 48
typedef struct { char name[16]; DWORD offset,size,crc; } Item;
static HANDLE src; static Item items[MAXF]; static DWORD count,base;
static char self[MAX_PATH],tmp[MAX_PATH],path[MAX_PATH],cmd[1024];
static BYTE buf[65536];
static DWORD slen(const char*s){DWORD n=0;while(s[n])n++;return n;}
static void scopy(char*d,const char*s){while(*s)*d++=*s++;*d=0;}
static void scat(char*d,const char*s){while(*d)d++;while(*s)*d++=*s++;*d=0;}
static int read_at(DWORD off,void*p,DWORD n){DWORD got;if(SetFilePointer(src,off,0,FILE_BEGIN)==INVALID_SET_FILE_POINTER)return 0;return ReadFile(src,p,n,&got,0)&&got==n;}
static int try_base(const char*b){
 DWORD k=slen(b),t=GetTickCount();const char*h="0123456789ABCDEF";int s;
 if(k>=sizeof(tmp)-16)return 0;
 scopy(tmp,b);if(k&&tmp[k-1]!='\\'){tmp[k++]='\\';tmp[k]=0;}
 scopy(tmp+k,"RFP");k+=3;for(s=16;s>=0;s-=4)tmp[k++]=h[(t>>s)&0xf];tmp[k]=0;
 return CreateDirectoryA(tmp,0)||GetLastError()==ERROR_ALREADY_EXISTS;
}
static void make_path(const char*name){scopy(path,tmp);scat(path,"\\");{DWORD i=slen(path),j=0;while(name[j]){if(name[j]=='\\'){path[i]=0;CreateDirectoryA(path,0);}path[i++]=name[j++];}path[i]=0;}}
static int extract_all(void){DWORD i,n;
 for(i=0;i<count;i++){HANDLE h;DWORD pos=base+items[i].offset,rem=items[i].size,wrote;
   make_path(items[i].name);
   h=CreateFileA(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
   if(h==INVALID_HANDLE_VALUE)return 0;
   while(rem){n=rem>sizeof(buf)?sizeof(buf):rem;if(!read_at(pos,buf,n)){CloseHandle(h);return 0;}if(!WriteFile(h,buf,n,&wrote,0)||wrote!=n){CloseHandle(h);return 0;}pos+=n;rem-=n;}
   CloseHandle(h);}return 1;}
static void cleanup(void){DWORD i;for(i=0;i<count;i++){make_path(items[i].name);DeleteFileA(path);}
 for(i=0;i<count;i++){char*b=items[i].name;DWORD j=0,k=0;while(b[j]){if(b[j]=='\\')k=j;j++;}if(k){scopy(path,tmp);scat(path,"\\");{DWORD l=slen(path),m=0;while(m<k){path[l+m]=b[m];m++;}path[l+k]=0;}RemoveDirectoryA(path);}}
 RemoveDirectoryA(tmp);}
static int has_native_folder_api(void){static const char*d[2]={"SHELL32.DLL","SHFOLDER.DLL"};int i;
 for(i=0;i<2;i++){HMODULE h=LoadLibraryA(d[i]);if(h){FARPROC f=GetProcAddress(h,"SHGetFolderPathW");FreeLibrary(h);if(f)return 1;}}return 0;}
static int dir_ok(const char*p){DWORD a=GetFileAttributesA(p);if(a!=0xFFFFFFFF)return (a&FILE_ATTRIBUTE_DIRECTORY)!=0;if(CreateDirectoryA(p,0))return 1;a=GetFileAttributesA(p);return a!=0xFFFFFFFF&&(a&FILE_ATTRIBUTE_DIRECTORY);}
static int tree_ok(const char*p){char part[MAX_PATH];DWORD i,n=slen(p);if(n<3||n>=MAX_PATH)return 0;scopy(part,p);for(i=3;i<n;i++)if(part[i]=='\\'){part[i]=0;if(!dir_ok(part))return 0;part[i]='\\';}return dir_ok(part);}
#ifdef FP32_PORT
/* Same user key as the installed FP32 launcher: the full SID in 8-character
   hex pieces on NT, "Default" on Windows 9x. */
static int user_key(char*path){
 typedef BOOL(WINAPI *OpenToken)(HANDLE,DWORD,PHANDLE);
 typedef BOOL(WINAPI *TokenInfo)(HANDLE,TOKEN_INFORMATION_CLASS,LPVOID,DWORD,PDWORD);
 typedef DWORD(WINAPI *SidLength)(PSID);
 DWORD raw[128],needed,n,i,k;BYTE*sid;HANDLE token;HMODULE adv;OpenToken ot;TokenInfo ti;SidLength sl;char chunk[10];
 if(GetVersion()&0x80000000u){if(slen(path)+9>=MAX_PATH)return 0;scat(path,"\\Default");return 1;}
 adv=LoadLibraryA("ADVAPI32.DLL");if(!adv)return 0;
 ot=(OpenToken)GetProcAddress(adv,"OpenProcessToken");ti=(TokenInfo)GetProcAddress(adv,"GetTokenInformation");sl=(SidLength)GetProcAddress(adv,"GetLengthSid");
 if(!ot||!ti||!sl||!ot(GetCurrentProcess(),TOKEN_QUERY,&token))return 0;
 if(!ti(token,TokenUser,raw,sizeof(raw),&needed)){CloseHandle(token);return 0;}
 CloseHandle(token);sid=(BYTE*)((TOKEN_USER*)raw)->User.Sid;n=sl(sid);
 if(n<8||n>68||sid<(BYTE*)raw||sid+n>(BYTE*)raw+sizeof(raw))return 0;
 for(i=0;i<n;){chunk[0]='\\';k=1;while(i<n&&k<9){BYTE v=sid[i++];chunk[k++]="0123456789ABCDEF"[v>>4];chunk[k++]="0123456789ABCDEF"[v&15];}chunk[k]=0;if(slen(path)+k>=MAX_PATH)return 0;scat(path,chunk);}
 return 1;}
#endif
/* Pick <portable folder>\\<name>, or %TEMP%\\<name> if that is read-only. */
static int save_base(char*out,const char*name){char d[MAX_PATH];DWORD n;
 scopy(d,self);n=slen(d);while(n&&d[n]!='\\')n--;d[n]=0;
 if(d[0]&&slen(d)+slen(name)+2<MAX_PATH-80){scopy(out,d);scat(out,"\\");scat(out,name);if(dir_ok(out))return 1;}
 n=GetTempPathA(MAX_PATH-80,d);if(!n||n>=MAX_PATH-80)return 0;if(d[n-1]!='\\')scat(d,"\\");scopy(out,d);scat(out,name);return dir_ok(out);}
static void prepare_saves(void){char b[MAX_PATH];
 if(has_native_folder_api())return;
#ifdef FP32_PORT
 if(save_base(b,"FP32DATA")&&user_key(b)&&tree_ok(b))SetEnvironmentVariableA("FP32_USERDATA",b);
#else
 if(save_base(b,"FP10DATA"))SetEnvironmentVariableA("FP10_USERDATA_ROOT",b);
#endif
}
void WINAPI entry(void){
 DWORD size,trailer[3],i;STARTUPINFOA si;PROCESS_INFORMATION pi;char*s;DWORD exitcode=1;char exepath[MAX_PATH];
 GetModuleFileNameA(0,self,sizeof(self));
 src=CreateFileA(self,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
 if(src==INVALID_HANDLE_VALUE){MessageBoxA(0,"Cannot open the portable package.","Portable",MB_ICONSTOP);ExitProcess(1);}
 size=GetFileSize(src,0);
 if(size==INVALID_FILE_SIZE||size<12||!read_at(size-12,trailer,12)||trailer[0]!=FKP_MAGIC){MessageBoxA(0,"Invalid portable package.","Portable",MB_ICONSTOP);ExitProcess(1);}
 base=trailer[1];count=trailer[2];
 if(!count||count>MAXF||!read_at(base,items,count*sizeof(Item))){MessageBoxA(0,"Invalid portable table.","Portable",MB_ICONSTOP);ExitProcess(1);}
 {char tb[MAX_PATH];DWORD made=0;
  if(GetTempPathA(sizeof(tb)-24,tb)&&tb[0]&&try_base(tb))made=1;
  if(!made){char d[MAX_PATH];DWORD n;scopy(d,self);n=slen(d);while(n&&d[n]!='\\')n--;d[n]=0;if(d[0]&&try_base(d))made=1;}
  if(!made&&try_base("C:\\"))made=1;
  if(!made){MessageBoxA(0,"Cannot create the temp folder.","Portable",MB_ICONSTOP);CloseHandle(src);ExitProcess(1);}}
 if(!extract_all()){cleanup();CloseHandle(src);MessageBoxA(0,"Cannot unpack the portable player.","Portable",MB_ICONSTOP);ExitProcess(1);}
 CloseHandle(src);
 scopy(exepath,tmp);scat(exepath,"\\" RUN_EXE);
 scopy(cmd,"\"");scat(cmd,exepath);scat(cmd,"\"");
 s=GetCommandLineA();if(*s=='"'){s++;while(*s&&*s!='"')s++;if(*s)s++;}else{while(*s&&*s!=' ')s++;}
 while(*s==' ')s++;if(*s){scat(cmd," ");scat(cmd,s);}
 prepare_saves();
 for(i=0;i<sizeof(si);i++)((BYTE*)&si)[i]=0;si.cb=sizeof(si);
 if(CreateProcessA(exepath,cmd,0,0,FALSE,0,0,tmp,&si,&pi)){
   WaitForSingleObject(pi.hProcess,INFINITE);GetExitCodeProcess(pi.hProcess,&exitcode);
   CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 } else MessageBoxA(0,"Cannot start the player.","Portable",MB_ICONSTOP);
 cleanup();
 ExitProcess(exitcode);
}
