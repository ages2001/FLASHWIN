#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* DBGRUN.EXE - minimal crash catcher for legacy Windows (NT 3.1+, 9x, 2000).
   Usage:  DBGRUN.EXE <program> [arguments]
   Runs the program (and any child it starts) under the Win32 Debug API and
   appends a report to DBGLOG.TXT next to DBGRUN.EXE: every module load (base,
   size, export name), every exception (code, address, module+offset, access
   info, registers, code bytes, EBP chain and a stack scan resolved to
   module+offset), debug strings and the exit code. Writes are write-through so
   the report survives a hard reset. No CRT. */
#define MAXMOD 256
#define MAXTHR 128
#define MAXPROC 8
typedef struct { DWORD pid,base,size; char name[40]; } Mod;
typedef struct { DWORD tid; HANDLE h; } Thr;
typedef struct { DWORD pid; HANDLE h; int bp_seen; } Proc;
static Mod mods[MAXMOD]; static int nmods;
static Thr thrs[MAXTHR]; static int nthrs;
static Proc procs[MAXPROC]; static int nprocs;
static HANDLE logf; static DWORD t0; static int nfirst, ndbgstr;
static char line[1024];
static void outl(void);
typedef LONG (WINAPI*NtQip)(HANDLE,DWORD,PVOID,ULONG,PULONG);
/* Periodic resource sample of every debuggee (VM_COUNTERS, handle count) and
   of the system, so slow leaks show up in the log before a crash. */
static void sample(void){static NtQip q;static int tried;int i;DWORD vm[11],hc;MEMORYSTATUS ms;
 if(!tried){tried=1;q=(NtQip)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQueryInformationProcess");}
 ms.dwLength=sizeof(ms);GlobalMemoryStatus(&ms);
 for(i=0;i<nprocs;i++){if(!procs[i].h)continue;
  for(hc=0;hc<11;hc++)vm[hc]=0;hc=0xffffffff;
  if(q){q(procs[i].h,3,vm,sizeof(vm),0);if(q(procs[i].h,20,&hc,4,0)<0)hc=0xffffffff;}
  wsprintfA(line,"[%6lu] pid %lu SAMPLE vsize %luK peakv %luK ws %luK pagefile %luK paged %lu nonpaged %lu handles %ld threads %d | sys availphys %luK availpage %luK load %lu",
   GetTickCount()-t0,procs[i].pid,vm[1]/1024,vm[0]/1024,vm[4]/1024,vm[9]/1024,vm[6],vm[8],(long)hc,nthrs,ms.dwAvailPhys/1024,ms.dwAvailPageFile/1024,ms.dwMemoryLoad);outl();}}

static void out(const char*s){DWORD n=0,w;while(s[n])n++;WriteFile(logf,s,n,&w,0);}
static void outl(void){out(line);out("\r\n");}
static HANDLE proc_handle(DWORD pid){int i;for(i=0;i<nprocs;i++)if(procs[i].pid==pid)return procs[i].h;return 0;}
static Proc* proc_rec(DWORD pid){int i;for(i=0;i<nprocs;i++)if(procs[i].pid==pid)return &procs[i];return 0;}
static HANDLE thr_handle(DWORD tid){int i;for(i=0;i<nthrs;i++)if(thrs[i].tid==tid)return thrs[i].h;return 0;}
static int rd(HANDLE p,DWORD a,void*b,DWORD n){DWORD got=0;return ReadProcessMemory(p,(LPCVOID)a,b,n,&got)&&got==n;}

/* read SizeOfImage and the export-directory name of the module at base */
static void mod_info(HANDLE p,DWORD base,DWORD*size,char*name,int cap){
 BYTE dos[64],pe[248];DWORD lfa,erva,nrva;char s[40];int i;
 *size=0;name[0]='?';name[1]=0;
 if(!rd(p,base,dos,64)||dos[0]!='M'||dos[1]!='Z')return;
 lfa=*(DWORD*)(dos+0x3c);if(!rd(p,base+lfa,pe,248)||pe[0]!='P'||pe[1]!='E')return;
 *size=*(DWORD*)(pe+0x18+0x38);erva=*(DWORD*)(pe+0x18+0x60);
 if(erva&&rd(p,base+erva+12,&nrva,4)&&nrva){for(i=0;i<(int)sizeof(s)-1;i++){if(!rd(p,base+nrva+i,&s[i],1))break;if(!s[i])break;}s[i]=0;
  if(i>0){for(i=0;s[i]&&i<cap-1;i++)name[i]=s[i];name[i]=0;}}
}
static void add_mod(DWORD pid,HANDLE p,DWORD base,const char*fallback){
 Mod*m;if(nmods>=MAXMOD)return;m=&mods[nmods++];m->pid=pid;m->base=base;
 mod_info(p,base,&m->size,m->name,sizeof(m->name));
 if(m->name[0]=='?'&&fallback){int i;for(i=0;fallback[i]&&i<39;i++)m->name[i]=fallback[i];m->name[i]=0;}
 wsprintfA(line,"[%6lu] pid %lu LOAD %08lX size %06lX %s",GetTickCount()-t0,pid,base,m->size,m->name);outl();
}
static void del_mod(DWORD pid,DWORD base){int i;for(i=0;i<nmods;i++)if(mods[i].pid==pid&&mods[i].base==base){
 wsprintfA(line,"[%6lu] pid %lu UNLOAD %08lX %s",GetTickCount()-t0,pid,base,mods[i].name);outl();mods[i]=mods[--nmods];return;}}
static const char* where(DWORD pid,DWORD a,DWORD*off){int i;for(i=0;i<nmods;i++)if(mods[i].pid==pid&&a>=mods[i].base&&a<mods[i].base+(mods[i].size?mods[i].size:0x10000)){*off=a-mods[i].base;return mods[i].name;}*off=a;return 0;}
static void sym(DWORD pid,DWORD a,char*o){DWORD off;const char*n=where(pid,a,&off);if(n)wsprintfA(o,"%s+%lX",n,off);else wsprintfA(o,"%08lX",a);}

static void dump_exception(DEBUG_EVENT*e){
 EXCEPTION_RECORD*r=&e->u.Exception.ExceptionRecord;HANDLE p=proc_handle(e->dwProcessId),th=thr_handle(e->dwThreadId);
 CONTEXT c;char s1[64],s2[64];BYTE code[16];DWORD i,v,ebp,frame[2];
 sym(e->dwProcessId,(DWORD)r->ExceptionAddress,s1);
 wsprintfA(line,"[%6lu] pid %lu tid %lu EXCEPTION %08lX at %s (%s chance)",GetTickCount()-t0,e->dwProcessId,e->dwThreadId,
   r->ExceptionCode,s1,e->u.Exception.dwFirstChance?"first":"SECOND");outl();
 if(r->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&r->NumberParameters>=2){
  wsprintfA(line,"    access: %s %08lX",r->ExceptionInformation[0]?"WRITE":"READ",r->ExceptionInformation[1]);outl();}
 if(!p||!th)return;
 c.ContextFlags=CONTEXT_FULL;if(!GetThreadContext(th,&c))return;
 wsprintfA(line,"    EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX",c.Eax,c.Ebx,c.Ecx,c.Edx,c.Esi,c.Edi);outl();
 wsprintfA(line,"    EIP=%08lX ESP=%08lX EBP=%08lX EFL=%08lX",c.Eip,c.Esp,c.Ebp,c.EFlags);outl();
 if(rd(p,c.Eip,code,16)){char*q=line;q+=wsprintfA(q,"    code:");for(i=0;i<16;i++)q+=wsprintfA(q," %02X",code[i]);outl();}
 /* EBP chain */
 ebp=c.Ebp;for(i=0;i<24&&ebp;i++){if(!rd(p,ebp,frame,8))break;sym(e->dwProcessId,frame[1],s2);
  wsprintfA(line,"    frame %2lu: ret %s",i,s2);outl();if(frame[0]<=ebp)break;ebp=frame[0];}
 /* stack scan: values that land inside a known module */
 {int n=0;for(i=0;i<160&&n<24;i++){DWORD off;if(!rd(p,c.Esp+i*4,&v,4))break;if(where(e->dwProcessId,v,&off)){sym(e->dwProcessId,v,s2);
   wsprintfA(line,"    stack[ESP+%03lX] %s",i*4,s2);outl();n++;}}}
}

void WINAPI entry(void){
 char self[MAX_PATH],logp[MAX_PATH],*cmd,*s;DWORD i,k,mode;STARTUPINFOA si;PROCESS_INFORMATION pi;DEBUG_EVENT e;int live=0;
 GetModuleFileNameA(0,self,sizeof(self));for(i=0;self[i];i++)logp[i]=self[i];logp[i]=0;
 k=i;while(k&&logp[k]!='\\')k--;logp[k]=0;
 {const char*n="\\DBGLOG.TXT";for(i=0;n[i];i++)logp[k+i]=n[i];logp[k+i]=0;}
 logf=CreateFileA(logp,GENERIC_WRITE,FILE_SHARE_READ,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,0);
 if(logf==INVALID_HANDLE_VALUE)ExitProcess(2);
 SetFilePointer(logf,0,0,FILE_END);t0=GetTickCount();
 s=GetCommandLineA();if(*s=='"'){s++;while(*s&&*s!='"')s++;if(*s)s++;}else while(*s&&*s!=' ')s++;
 while(*s==' ')s++;cmd=s;
 wsprintfA(line,"==== DBGRUN start, version %08lX, cmd: %s",GetVersion(),cmd);outl();
 if(!*cmd){out("no program given\r\n");ExitProcess(1);}
 for(i=0;i<sizeof(si);i++)((BYTE*)&si)[i]=0;si.cb=sizeof(si);
 /* working directory = folder of the program, like a shortcut would */
 {static char wd[MAX_PATH];char*q=cmd,*d=wd;int quoted=(*q=='"');if(quoted)q++;
  while(*q&&(quoted?*q!='"':*q!=' ')&&d<wd+MAX_PATH-1)*d++=*q++;*d=0;
  k=0;for(i=0;wd[i];i++)if(wd[i]=='\\')k=i;if(k)wd[k]=0;else wd[0]=0;
  if(!CreateProcessA(0,cmd,0,0,FALSE,DEBUG_PROCESS,0,wd[0]?wd:0,&si,&pi)){
   wsprintfA(line,"CreateProcess failed, error %lu",GetLastError());outl();CloseHandle(logf);ExitProcess(1);}}
 for(;;){
  if(!WaitForDebugEvent(&e,15000)){if(GetLastError()==ERROR_SEM_TIMEOUT||GetLastError()==WAIT_TIMEOUT||1){sample();continue;}break;}
  mode=DBG_CONTINUE;
  switch(e.dwDebugEventCode){
  case CREATE_PROCESS_DEBUG_EVENT:
   if(nprocs<MAXPROC){procs[nprocs].pid=e.dwProcessId;procs[nprocs].h=e.u.CreateProcessInfo.hProcess;procs[nprocs].bp_seen=0;nprocs++;}
   if(nthrs<MAXTHR){thrs[nthrs].tid=e.dwThreadId;thrs[nthrs].h=e.u.CreateProcessInfo.hThread;nthrs++;}
   live++;add_mod(e.dwProcessId,e.u.CreateProcessInfo.hProcess,(DWORD)e.u.CreateProcessInfo.lpBaseOfImage,"<exe>");
   if(e.u.CreateProcessInfo.hFile)CloseHandle(e.u.CreateProcessInfo.hFile);break;
  case CREATE_THREAD_DEBUG_EVENT:
   if(nthrs<MAXTHR){thrs[nthrs].tid=e.dwThreadId;thrs[nthrs].h=e.u.CreateThread.hThread;nthrs++;}
   {char s1[64];sym(e.dwProcessId,(DWORD)e.u.CreateThread.lpStartAddress,s1);
    wsprintfA(line,"[%6lu] pid %lu THREAD %lu start %s",GetTickCount()-t0,e.dwProcessId,e.dwThreadId,s1);outl();}break;
  case EXIT_THREAD_DEBUG_EVENT:
   for(i=0;i<(DWORD)nthrs;i++)if(thrs[i].tid==e.dwThreadId){thrs[i]=thrs[--nthrs];break;}
   wsprintfA(line,"[%6lu] pid %lu THREAD %lu exit %lu",GetTickCount()-t0,e.dwProcessId,e.dwThreadId,e.u.ExitThread.dwExitCode);outl();break;
  case LOAD_DLL_DEBUG_EVENT:
   add_mod(e.dwProcessId,proc_handle(e.dwProcessId),(DWORD)e.u.LoadDll.lpBaseOfDll,0);
   if(e.u.LoadDll.hFile)CloseHandle(e.u.LoadDll.hFile);break;
  case UNLOAD_DLL_DEBUG_EVENT:del_mod(e.dwProcessId,(DWORD)e.u.UnloadDll.lpBaseOfDll);break;
  case OUTPUT_DEBUG_STRING_EVENT:
   if(ndbgstr<400){char b[240];DWORD n=e.u.DebugString.nDebugStringLength;HANDLE p=proc_handle(e.dwProcessId);
    if(n>sizeof(b)-1)n=sizeof(b)-1;if(p&&!e.u.DebugString.fUnicode&&rd(p,(DWORD)e.u.DebugString.lpDebugStringData,b,n)){b[n]=0;
     for(i=0;b[i];i++)if(b[i]=='\r'||b[i]=='\n')b[i]=' ';wsprintfA(line,"[%6lu] pid %lu DBGSTR %s",GetTickCount()-t0,e.dwProcessId,b);outl();ndbgstr++;}}break;
  case EXCEPTION_DEBUG_EVENT:{
   DWORD code=e.u.Exception.ExceptionRecord.ExceptionCode;Proc*pr=proc_rec(e.dwProcessId);
   if(code==EXCEPTION_BREAKPOINT&&pr&&!pr->bp_seen){pr->bp_seen=1;
    wsprintfA(line,"[%6lu] pid %lu initial breakpoint (process init complete)",GetTickCount()-t0,e.dwProcessId);outl();break;}
   if(!e.u.Exception.dwFirstChance||nfirst<48){dump_exception(&e);if(e.u.Exception.dwFirstChance)nfirst++;}
   mode=DBG_EXCEPTION_NOT_HANDLED;break;}
  case EXIT_PROCESS_DEBUG_EVENT:
   wsprintfA(line,"[%6lu] pid %lu EXIT code %08lX",GetTickCount()-t0,e.dwProcessId,e.u.ExitProcess.dwExitCode);outl();
   for(i=0;i<(DWORD)nmods;)if(mods[i].pid==e.dwProcessId)mods[i]=mods[--nmods];else i++;
   for(i=0;i<(DWORD)nprocs;i++)if(procs[i].pid==e.dwProcessId)procs[i].h=0;
   live--;break;
  }
  ContinueDebugEvent(e.dwProcessId,e.dwThreadId,mode);
  if(e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT&&live<=0)break;
 }
 out("==== DBGRUN end\r\n");FlushFileBuffers(logf);CloseHandle(logf);ExitProcess(0);
}
