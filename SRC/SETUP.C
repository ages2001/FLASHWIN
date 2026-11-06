/* NT 3.1 development installer. No CRT, no SSE, no system DLL replacement. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddeml.h>
#include "product.h"
typedef struct {char name[16];DWORD offset,size,crc;} Item;
typedef struct {DWORD magic,created_dir,had_key,had_value,type,size;BYTE previous[4096];} State;
static char root[MAX_PATH]=DEFAULT_INSTALL_ROOT,self[MAX_PATH],path[MAX_PATH],command[1024],error[256];
static Item items[PACKAGE_MAX_FILES];static DWORD count,base,completed,group_created,associated,class_created;
static DWORD os_major=3;static HKEY class_hive=HKEY_CLASSES_ROOT;static char class_pre[40]="";
static BYTE buffer[32768];static State state;static HANDLE source;static DWORD dde;static HCONV conv;
static DWORD len(const char*s){DWORD n=0;while(s[n])n++;return n;}
static void copy(char*d,const char*s){while((*d++=*s++));}
static void cat(char*d,const char*s){copy(d+len(d),s);}
static int equal(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
#include "progress.h"
#include "wizard.h"
static void file_path(const char*n){copy(path,root);cat(path,"\\");cat(path,n);}
static int fail(const char*s){copy(error,s);return 0;}
static int read_at(DWORD off,void*p,DWORD n){DWORD got;if(SetFilePointer(source,off,0,FILE_BEGIN)==INVALID_SET_FILE_POINTER)return 0;return ReadFile(source,p,n,&got,0)&&got==n;}
static DWORD crc(DWORD c,const BYTE*p,DWORD n){DWORD i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((0-(c&1))&0xedb88320);}return c;}
static int package(void){DWORD trailer[3],size=GetFileSize(source,0),i,j,end;if(size==INVALID_FILE_SIZE||size<12||!read_at(size-12,trailer,12)||trailer[0]!=0x31504b46)return fail("Invalid setup package.");base=trailer[1];count=trailer[2];if(!count||count>PACKAGE_MAX_FILES||base>size-12||count*sizeof(Item)>size-12-base||!read_at(base,items,count*sizeof(Item)))return fail("Invalid package table.");progress_written=0;progress_total=size-12-base-count*sizeof(Item);end=count*sizeof(Item);for(i=0;i<count;i++){DWORD remaining=items[i].size,pos=base+items[i].offset,c=~0u;if(items[i].offset!=end||items[i].size>size-12-base-end)return fail("Invalid package bounds.");end+=items[i].size;for(j=0;j<16&&items[i].name[j];j++){char x=items[i].name[j];if(!((x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='.'||(x=='\\'&&equal(items[i].name,PLAYER_CHILD))))return fail("Unsafe package file name.");}if(!j||j==16||equal(items[i].name,".")||equal(items[i].name,".."))return fail("Invalid package name.");while(remaining){DWORD n=remaining>sizeof(buffer)?sizeof(buffer):remaining;if(!read_at(pos,buffer,n))return fail("Cannot read package.");c=crc(c,buffer,n);pos+=n;remaining-=n;progress_written+=n;progress_file(progress_scale(progress_written,progress_total,30),"Verifying ",items[i].name);}if((c^~0u)!=items[i].crc)return fail("Package checksum mismatch.");}return end==size-12-base?1:fail("Unexpected package data.");}
static HDDEDATA CALLBACK callback(UINT t,UINT f,HCONV c,HSZ a,HSZ b,HDDEDATA d,ULONG_PTR x,ULONG_PTR y){return 0;}
static int shell_connect(void){HSZ s;if(DdeInitializeA(&dde,callback,APPCMD_CLIENTONLY,0)!=DMLERR_NO_ERROR)return fail("Cannot initialize Program Manager connection.");s=DdeCreateStringHandleA(dde,"PROGMAN",CP_WINANSI);conv=DdeConnect(dde,s,s,0);DdeFreeStringHandle(dde,s);return conv?1:fail("Program Manager must be running.");}
/* The shell may still be opening the new group window; allow time
   (Windows 9x Explorer can take longer than 10 s on slow machines). A retry
   could add a duplicate item, so wait longer instead. */
static int shell_command(const char*s){return DdeClientTransaction((BYTE*)s,len(s)+1,conv,0,0,XTYP_EXECUTE,60000,0)!=0;}
static int group_exists(void){HSZ item=DdeCreateStringHandleA(dde,"Groups",CP_WINANSI);HDDEDATA d=DdeClientTransaction(0,0,conv,item,CF_TEXT,XTYP_REQUEST,10000,0);DWORD n,i,k;char *p;DdeFreeStringHandle(dde,item);if(!d)return -1;p=(char*)DdeAccessData(d,&n);if(!p){DdeFreeDataHandle(d);return -1;}for(i=0;i<n;i++){for(k=0;k<len(GROUP)&&i+k<n&&p[i+k]==GROUP[k];k++){}if(k==len(GROUP)&&(i==0||p[i-1]=='\n'||p[i-1]=='\r')&&(i+k==n||p[i+k]=='\r'||p[i+k]=='\n'||p[i+k]==0)){DdeUnaccessData(d);DdeFreeDataHandle(d);return 1;}}DdeUnaccessData(d);DdeFreeDataHandle(d);return 0;}
static void choose_class_root(void){HKEY h;char probe[80];copy(probe,CLASS);cat(probe,"_Ins");if(RegCreateKeyA(HKEY_CLASSES_ROOT,probe,&h)==ERROR_SUCCESS){RegCloseKey(h);RegDeleteKeyA(HKEY_CLASSES_ROOT,probe);class_hive=HKEY_CLASSES_ROOT;class_pre[0]=0;}else{class_hive=HKEY_CURRENT_USER;copy(class_pre,"Software\\Classes\\");}}
static int set_value(const char*key,const char*value){HKEY h;char full[160];LONG r;copy(full,class_pre);cat(full,key);r=RegCreateKeyA(class_hive,full,&h);if(r)return 0;r=RegSetValueExA(h,0,0,REG_SZ,(BYTE*)value,len(value)+1);RegCloseKey(h);return r==0;}
/* NT 3.1's native shell quotes the substituted filename itself. Quoting %1
   again produces doubled quotes and splits a filename containing spaces. */
static void open_command(void){copy(command,"\"");cat(command,root);cat(command,"\\" PLAYER_EXE "\" %1");}
static int backup_association(void){HKEY h;LONG r;char k[160];
 /* Take over the SWF association even when it is already owned - by a previous
    install of this player (our own class already present) or by another program.
    A pre-existing class is no longer an error: install() re-creates/overwrites
    our class keys, so we simply assume ownership. */
 copy(k,class_pre);cat(k,".swf");r=RegOpenKeyExA(class_hive,k,0,KEY_READ,&h);if(r==ERROR_FILE_NOT_FOUND)return 1;if(r)return fail("Cannot read the existing SWF association.");state.had_key=1;state.size=sizeof(state.previous);r=RegQueryValueExA(h,0,0,&state.type,state.previous,&state.size);RegCloseKey(h);if(r==ERROR_FILE_NOT_FOUND){state.size=0;return 1;}if(r)return fail("Cannot back up the existing SWF association.");
 /* If .swf already points at our own class (a prior install of this player),
    there is no foreign owner to restore on uninstall, so do not save it. */
 if(state.type==REG_SZ&&state.size==sizeof(CLASS)&&equal((char*)state.previous,CLASS))state.had_value=0;else state.had_value=1;return 1;}
static int restore_association(void){HKEY h;BYTE v[128];DWORD n=sizeof(v),type;char k[160];LONG r;copy(k,class_pre);cat(k,".swf");r=RegOpenKeyExA(class_hive,k,0,KEY_READ|KEY_WRITE,&h);if(r==ERROR_FILE_NOT_FOUND)return 1;if(r)return 0;r=RegQueryValueExA(h,0,0,&type,v,&n);if(!r&&type==REG_SZ&&n==sizeof(CLASS)&&equal((char*)v,CLASS)){if(state.had_value)r=RegSetValueExA(h,0,0,state.type,state.previous,state.size);else r=RegDeleteValueA(h,0);}else r=0;RegCloseKey(h);/* Keep an empty extension key: unrelated subkeys must survive. */return r==0||r==ERROR_FILE_NOT_FOUND;}
static void del_class(const char*sub){char k[160];copy(k,class_pre);cat(k,sub);RegDeleteKeyA(class_hive,k);}
static void remove_class(void){del_class(CLASS "\\shell\\open\\command");del_class(CLASS "\\shell\\open");del_class(CLASS "\\shell");del_class(CLASS "\\DefaultIcon");del_class(CLASS);}
static int write_state(void){HANDLE h;DWORD n;file_path("INSTALL.DAT");h=CreateFileA(path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)return fail("Cannot save uninstall state.");state.magic=0x31545346;WriteFile(h,&state,sizeof(state),&n,0);FlushFileBuffers(h);CloseHandle(h);return n==sizeof(state)?1:fail("Cannot write uninstall state.");}
static int extract(DWORD i){HANDLE h;DWORD left=items[i].size,pos=base+items[i].offset,n,w;file_path(items[i].name);h=CreateFileA(path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)return fail("Cannot create an installed file. Existing files are never overwritten.");completed=i+1;while(left){n=left>sizeof(buffer)?sizeof(buffer):left;if(!read_at(pos,buffer,n)||!WriteFile(h,buffer,n,&w,0)||w!=n){CloseHandle(h);return fail("Cannot extract an installed file.");}left-=n;pos+=n;progress_written+=n;progress_file(30+progress_scale(progress_written,progress_total,55),"Installing ",items[i].name);}FlushFileBuffers(h);CloseHandle(h);return 1;}
static int child_absent(void){
#ifdef FP32_SETUP
 file_path("APP");if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)return fail("The APP subfolder already exists. Choose another destination.");
#endif
 return 1;
}
static int create_child(void){
#ifdef FP32_SETUP
 file_path("APP");if(!CreateDirectoryA(path,0))return fail("Cannot create the APP subfolder.");state.created_dir|=2;
#endif
 return 1;
}
static void remove_child(void){
#ifdef FP32_SETUP
 if(state.created_dir&2){file_path("APP");RemoveDirectoryA(path);}
#endif
}
/* FP32 setup does NOT install or register WINSIMD. The SSE2/XMM-preservation
   driver is a separate component the user installs themselves (BIN\WINSIMD). */
/* Program Manager DDE cannot quote a command line; the 9x shell then splits
   "C:\\Program Files\\..\\UNINST.EXE /uninstall" at the first space. Use the
   short (8.3) form of the folder when the system provides it. */
static void cat_short_root(char*d){char s[MAX_PATH];typedef DWORD(WINAPI*GSPN)(LPCSTR,LPSTR,DWORD);GSPN g=(GSPN)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"GetShortPathNameA");DWORD n=g?g(root,s,sizeof(s)):0;cat(d,(n&&n<sizeof(s))?s:root);}
static int install(void){DWORD i;if(!package()||!shell_connect())return 0;i=group_exists();if(i!=0)return fail(i==1?"The program group already exists. No changes were made.":"Cannot inspect Program Manager groups.");if(!backup_association()||!child_absent())return 0;for(i=0;i<count;i++){file_path(items[i].name);if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)return fail("An installation target file already exists. No files were overwritten.");}file_path("UNINST.EXE");if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)return fail("An uninstaller already exists.");file_path("INSTALL.DAT");if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)return fail("Uninstall state already exists.");if(CreateDirectoryA(root,0))state.created_dir=1;else if(GetLastError()!=ERROR_ALREADY_EXISTS)return fail("Cannot create the installation directory.");if(!create_child()||!write_state())return 0;progress_written=0;for(i=0;i<count;i++)if(!extract(i))return 0;progress_update(87,"Installing the uninstaller...");file_path("UNINST.EXE");if(!CopyFileA(self,path,TRUE))return fail("Cannot install the uninstaller.");progress_update(92,"Registering SWF files...");if(!set_value(CLASS,PRODUCT " movie"))return fail("Cannot create the SWF file class.");class_created=1;open_command();if(!set_value(CLASS "\\shell\\open\\command",command))return fail("Cannot register the open command.");copy(command,root);cat(command,"\\" PLAYER_ICON ",0");if(!set_value(CLASS "\\DefaultIcon",command))return fail("Cannot register the file icon.");associated=1;if(!set_value(".swf",CLASS))return fail("Cannot register the SWF extension.");progress_update(96,"Creating program shortcuts...");if(!shell_command("[CreateGroup(\"" GROUP "\")]"))return fail("Cannot create the program group.");group_created=1;copy(command,"[AddItem(\"");cat(command,root);cat(command,"\\" PLAYER_EXE "\",\"" PRODUCT "\",\"");cat(command,root);cat(command,"\\" PLAYER_ICON "\",0)]");if(!shell_command(command))return fail("Cannot create the player shortcut.");copy(command,"[AddItem(\"");cat_short_root(command);cat(command,"\\UNINST.EXE /uninstall\",\"Uninstall " PRODUCT "\",\"");cat(command,root);cat(command,"\\UNINST.EXE\",1)]");if(!shell_command(command))return fail("Cannot create the uninstall shortcut.");return 1;}
static void rollback(void){DWORD i;if(group_created)shell_command("[DeleteGroup(\"" GROUP "\")]");if(associated)restore_association();if(class_created)remove_class();for(i=0;i<completed;i++){file_path(items[i].name);DeleteFileA(path);}/* Only run after INSTALL.DAT was created by this invocation. */if(state.magic==0x31545346){file_path("UNINST.EXE");DeleteFileA(path);file_path("INSTALL.DAT");DeleteFileA(path);}remove_child();if(state.created_dir&1)RemoveDirectoryA(root);}
static int verify_installed(void){DWORD i,n,c,total;HANDLE h;for(i=0;i<count;i++){progress_file(30+progress_scale(i,count,30),"Checking ",items[i].name);file_path(items[i].name);h=CreateFileA(path,GENERIC_READ|GENERIC_WRITE|DELETE,0,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)continue;return fail("A player file is in use or inaccessible. Close the player before uninstalling.");}c=~0u;total=0;for(;;){if(!ReadFile(h,buffer,sizeof(buffer),&n,0)){CloseHandle(h);return fail("Cannot verify an installed file.");}if(!n)break;c=crc(c,buffer,n);total+=n;}CloseHandle(h);if(total!=items[i].size||(c^~0u)!=items[i].crc)return fail("An installed file was modified. Preserve it before uninstalling; no changes were made.");}return 1;}
static int remove_shortcuts(void){HSZ item=DdeCreateStringHandleA(dde,GROUP,CP_WINANSI);HDDEDATA d=DdeClientTransaction(0,0,conv,item,CF_TEXT,XTYP_REQUEST,10000,0);DWORD n,i,j,other=0,player=0,uninstaller=0;char*p;const char*a="\"" PRODUCT "\",",*b="\"Uninstall " PRODUCT "\",";DdeFreeStringHandle(dde,item);if(!d){int g=group_exists();return g==0?1:fail("Cannot inspect the installed program group.");}p=(char*)DdeAccessData(d,&n);if(!p){DdeFreeDataHandle(d);return fail("Cannot read the installed program group.");}i=0;while(i<n&&p[i]&&p[i]!='\n')i++;while(i<n){while(i<n&&(p[i]=='\r'||p[i]=='\n'||p[i]==0))i++;if(i>=n)break;for(j=0;j<len(a)&&i+j<n&&p[i+j]==a[j];j++){}if(j==len(a))player=1;else{for(j=0;j<len(b)&&i+j<n&&p[i+j]==b[j];j++){}if(j==len(b))uninstaller=1;else other=1;}while(i<n&&p[i]&&p[i]!='\n')i++;}DdeUnaccessData(d);DdeFreeDataHandle(d);if(!other)return shell_command("[DeleteGroup(\"" GROUP "\")]")?1:fail("Cannot remove the program group.");if(!shell_command("[ShowGroup(\"" GROUP "\",1)]"))return fail("Cannot open the program group.");if(player&&!shell_command("[DeleteItem(\"" PRODUCT "\")]"))return fail("Cannot remove the player shortcut.");if(uninstaller&&!shell_command("[DeleteItem(\"Uninstall " PRODUCT "\")]"))return fail("Cannot remove the uninstall shortcut.");return 1;}
/* Delete a file at the next restart. Windows NT: MoveFileEx. Windows 9x has
   no MoveFileEx; there WININIT.INI [rename] "NUL=<short path>" does it. */
static int delete_on_reboot(const char*file){char ini[MAX_PATH+16],shortp[MAX_PATH],*buf;HANDLE h;DWORD n,size,i,at;const char*hdr="[rename]";
 typedef DWORD(WINAPI*GSPN)(LPCSTR,LPSTR,DWORD);GSPN gs;
 if(!(GetVersion()&0x80000000u))return MoveFileExA(file,0,MOVEFILE_DELAY_UNTIL_REBOOT);
 gs=(GSPN)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"GetShortPathNameA");
 if(!gs||!gs(file,shortp,sizeof(shortp)))copy(shortp,file);
 n=GetWindowsDirectoryA(ini,MAX_PATH);if(!n||n>=MAX_PATH)return 0;if(ini[n-1]!='\\')cat(ini,"\\");cat(ini,"WININIT.INI");
 h=CreateFileA(ini,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);if(h==INVALID_HANDLE_VALUE)return 0;
 size=GetFileSize(h,0);if(size==0xFFFFFFFF||size>65536){CloseHandle(h);return 0;}
 buf=(char*)LocalAlloc(LMEM_FIXED,size+MAX_PATH+32);if(!buf){CloseHandle(h);return 0;}
 if(size&&(!ReadFile(h,buf,size,&n,0)||n!=size)){LocalFree(buf);CloseHandle(h);return 0;}
 /* find an existing [rename] section (case-insensitive) */
 at=0xFFFFFFFF;for(i=0;i+8<=size;i++){DWORD k;for(k=0;k<8&&(buf[i+k]|32)==(hdr[k]|32);k++){}if(k==8&&(i==0||buf[i-1]=='\n')){at=i+8;while(at<size&&buf[at]!='\n')at++;if(at<size)at++;break;}}
 {char line[MAX_PATH+16];DWORD ll;copy(line,"NUL=");cat(line,shortp);cat(line,"\r\n");ll=len(line);
  if(at==0xFFFFFFFF){char pre[16];copy(pre,(size&&buf[size-1]!='\n')?"\r\n[rename]\r\n":"[rename]\r\n");SetFilePointer(h,0,0,FILE_END);
   if(!WriteFile(h,pre,len(pre),&n,0)||!WriteFile(h,line,ll,&n,0)){LocalFree(buf);CloseHandle(h);return 0;}}
  else{SetFilePointer(h,at,0,FILE_BEGIN);if(!WriteFile(h,line,ll,&n,0)||(size>at&&!WriteFile(h,buf+at,size-at,&n,0))){LocalFree(buf);CloseHandle(h);return 0;}}}
 LocalFree(buf);FlushFileBuffers(h);CloseHandle(h);return 1;}
static int uninstall(void){HANDLE h;DWORD n,i;char expected[1024];HKEY key;DWORD type;if(!package()||!shell_connect())return 0;file_path("INSTALL.DAT");h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return fail("Uninstall state is missing.");i=ReadFile(h,&state,sizeof(state),&n,0);CloseHandle(h);if(!i||n!=sizeof(state)||state.magic!=0x31545346||state.size>sizeof(state.previous))return fail("Invalid uninstall state.");open_command();copy(expected,command);n=sizeof(command);if(RegOpenKeyExA(HKEY_CLASSES_ROOT,CLASS "\\shell\\open\\command",0,KEY_READ,&key))return fail("The registered installation changed; no files were removed.");i=RegQueryValueExA(key,0,0,&type,(BYTE*)command,&n);RegCloseKey(key);if(i||type!=REG_SZ||n!=len(expected)+1||!equal(command,expected))return fail("The registered installation changed; no files were removed.");if(!verify_installed()||!remove_shortcuts())return 0;if(!restore_association())return fail("Cannot restore the previous SWF association.");for(i=0;i<count;i++){progress_file(65+progress_scale(i,count,30),"Removing ",items[i].name);file_path(items[i].name);if(!DeleteFileA(path)&&GetLastError()!=ERROR_FILE_NOT_FOUND)return fail("A player file is in use. Close Flash Player before uninstalling.");}remove_child();file_path("UNINST.EXE");if(!delete_on_reboot(path))return fail("Cannot schedule uninstaller removal.");remove_class();file_path("INSTALL.DAT");DeleteFileA(path);return 1;}
/* The uninstaller can be started through an 8.3 path (Windows 9x
   shortcuts use one); expand the folder to its long form, component by
   component with FindFirstFile (available on every Windows version), so it
   matches the registered open command. */
static void long_root(void){char out[MAX_PATH],part[MAX_PATH];WIN32_FIND_DATAA fd;HANDLE h;DWORD i=0,o,k;
 if(len(root)<3||root[1]!=':'||root[2]!='\\')return;out[0]=root[0];out[1]=':';out[2]=0;o=2;i=2;
 while(root[i]=='\\'){DWORD start=++i;while(root[i]&&root[i]!='\\')i++;if(i==start)break;
  for(k=0;k<o;k++)part[k]=out[k];part[o]='\\';for(k=start;k<i;k++)part[o+1+k-start]=root[k];part[o+1+i-start]=0;
  h=FindFirstFileA(part,&fd);if(h==INVALID_HANDLE_VALUE){if(o+1+i-start>=MAX_PATH)return;copy(out,part);o=len(out);continue;}FindClose(h);
  if(o+1+len(fd.cFileName)>=MAX_PATH)return;out[o]='\\';copy(out+o+1,fd.cFileName);o=len(out);}
 if(!root[i])copy(root,out);}
void WINAPI entry(void){DWORD version=GetVersion(),i;int removing=0,ok;char*s=GetCommandLineA();GetModuleFileNameA(0,self,sizeof(self));if(*s=='"'){s++;while(*s&&*s!='"')s++;if(*s)s++;}else while(*s&&*s!=' ')s++;while(*s==' ')s++;removing=equal(s,"/uninstall");if(removing){copy(root,self);i=len(root);while(i&&root[i]!='\\')i--;root[i]=0;long_root();}/* OS restriction removed: installer no longer limited to Windows NT 3.1. */os_major=LOBYTE(LOWORD(version));if(!removing&&os_major>=4){DWORD pn=GetEnvironmentVariableA("ProgramFiles",root,sizeof(root)-40);if(!pn||pn>=sizeof(root)-40)copy(root,"C:\\Program Files");cat(root,"\\" MODERN_SUBDIR);}choose_class_root();if(removing){if(MessageBoxA(0,"Remove " PRODUCT " and restore the previous SWF association? Close the player first.",PRODUCT " Setup",MB_YESNO|MB_ICONQUESTION)!=IDYES)ExitProcess(0);}else{int chosen=wizard_begin();if(chosen<0){MessageBoxA(0,"Cannot create the setup wizard.","Setup failed",MB_ICONSTOP);ExitProcess(1);}if(!chosen)ExitProcess(0);}if(!progress_begin(removing)){progress_end();MessageBoxA(0,"Cannot create the setup progress window.","Setup failed",MB_ICONSTOP);ExitProcess(1);}source=CreateFileA(self,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(source==INVALID_HANDLE_VALUE){progress_end();MessageBoxA(0,"Cannot read setup executable.","Setup failed",MB_ICONSTOP);ExitProcess(1);}ok=removing?uninstall():install();if(!ok&&!removing){progress_update(progress_value,"Rolling back installation changes...");rollback();}CloseHandle(source);if(conv)DdeDisconnect(conv);if(dde)DdeUninitialize(dde);if(ok)progress_update(100,removing?"Removal complete.":"Installation complete.");progress_end();if(removing)MessageBoxA(0,ok?"Player files removed. Restart Windows to remove the uninstaller. Unrelated files and user-added shortcuts are retained.":error,ok?PRODUCT " Setup":"Setup failed",ok?MB_OK:MB_ICONSTOP);else{if(ok){copy(command,PRODUCT " was installed in ");cat(command,root);cat(command,".");}wizard_complete(ok,ok?command:error);}ExitProcess(ok?0:1);}
