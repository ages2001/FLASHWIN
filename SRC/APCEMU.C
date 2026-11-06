#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* User-mode APC emulation for NT 3.1 / 3.51, whose NTDLL has no
   NtQueueApcThread (so kernel32 has no QueueUserAPC). Flash uses
   QueueUserAPC only to wake a thread that waits in SleepEx(..., TRUE) in its
   condition-variable code. Without a working QueueUserAPC every such wait
   runs to its timeout (or forever for INFINITE).
   Emulation: each thread that enters an alertable SleepEx gets an
   auto-reset wake event and a small queue. QueueUserAPC finds the target
   thread's id from its handle (NtQueryInformationThread), queues the routine
   and sets the event; the waiter runs queued routines and returns
   WAIT_IO_COMPLETION, as a real APC delivery would. Routines run only on the
   target thread inside SleepEx, so APC semantics (no asynchronous
   interruption) are preserved. Where the OS has a native QueueUserAPC the
   native QueueUserAPC/SleepEx pair is used unchanged. No CRT. */
#define SLOTS 64
#define QLEN 16
typedef struct { DWORD tid; HANDLE wake; LONG head,tail; PAPCFUNC fn[QLEN]; ULONG_PTR arg[QLEN]; } Slot;
static Slot slots[SLOTS];
static volatile LONG guard;
static void lock(void){while(InterlockedExchange((LONG*)&guard,1))Sleep(0);}
static void unlock(void){InterlockedExchange((LONG*)&guard,0);}
typedef DWORD (WINAPI *NativeQueue)(PAPCFUNC,HANDLE,ULONG_PTR);
static NativeQueue native_queue(void){static int done;static NativeQueue f;if(!done){f=(NativeQueue)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"QueueUserAPC");done=1;}return f;}
/* Find (or create) the slot of a thread id. Caller holds the lock. */
static Slot*slot_of(DWORD tid,BOOL create){int i,free_slot=-1;
 for(i=0;i<SLOTS;i++){if(slots[i].tid==tid)return &slots[i];if(!slots[i].tid&&free_slot<0)free_slot=i;}
 if(!create||free_slot<0)return 0;
 if(!slots[free_slot].wake){slots[free_slot].wake=CreateEventA(0,FALSE,FALSE,0);if(!slots[free_slot].wake)return 0;}
 slots[free_slot].tid=tid;slots[free_slot].head=slots[free_slot].tail=0;return &slots[free_slot];}
static int run_pending(Slot*s){int ran=0;
 for(;;){PAPCFUNC fn;ULONG_PTR arg;lock();if(s->head==s->tail){unlock();break;}
  fn=s->fn[s->head%QLEN];arg=s->arg[s->head%QLEN];s->head++;unlock();
  if(fn)fn(arg);ran=1;}
 return ran;}
DWORD WINAPI fp_SleepEx(DWORD ms,BOOL alertable){Slot*s;DWORD r;
 if(!alertable||native_queue())return SleepEx(ms,alertable);
 lock();s=slot_of(GetCurrentThreadId(),TRUE);unlock();
 if(!s)return SleepEx(ms,FALSE);
 if(run_pending(s))return WAIT_IO_COMPLETION;
 r=WaitForSingleObject(s->wake,ms);
 if(run_pending(s))return WAIT_IO_COMPLETION;
 (void)r;return 0;}
typedef LONG (WINAPI *QueryThread)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef struct { LONG status; PVOID teb; HANDLE process,thread; ULONG affinity; LONG priority,base; } Basic;
DWORD WINAPI fp_QueueUserAPC2(PAPCFUNC fn,HANDLE thread,ULONG_PTR arg){NativeQueue nq=native_queue();QueryThread q;Basic b;ULONG got=0;Slot*s;DWORD tid;
 if(nq)return nq(fn,thread,arg);
 if(!fn){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 q=(QueryThread)GetProcAddress(GetModuleHandleA("NTDLL.DLL"),"NtQueryInformationThread");
 if(!q||q(thread,0,&b,sizeof(b),&got)<0||!b.thread){SetLastError(ERROR_INVALID_HANDLE);return 0;}
 tid=(DWORD)(ULONG_PTR)b.thread;
 lock();s=slot_of(tid,TRUE);
 if(!s||s->tail-s->head>=QLEN){unlock();SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}
 s->fn[s->tail%QLEN]=fn;s->arg[s->tail%QLEN]=arg;s->tail++;unlock();
 SetEvent(s->wake);return 1;}
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* NT 3.1 / 3.5x (and Windows 95) InterlockedIncrement / InterlockedDecrement
   return only the SIGN of the result (0, or a non-zero value of the right
   sign), not the new value. Flash's memory manager, reference counts and
   thread hand-off counters use the exact value, so on those systems two
   threads (e.g. sound decoding and the main thread) could both see "first"
   or "last" and corrupt the heap. These versions return the exact new value
   on every Windows version (lock xadd, i486 and later). No CRT. */
LONG WINAPI fp_InterlockedIncrement(volatile LONG *p){LONG v=1;__asm__ volatile("lock xaddl %0,%1":"+r"(v),"+m"(*p)::"memory","cc");return v+1;}
LONG WINAPI fp_InterlockedDecrement(volatile LONG *p){LONG v=-1;__asm__ volatile("lock xaddl %0,%1":"+r"(v),"+m"(*p)::"memory","cc");return v-1;}
/* FP32 keeps its engine in APP\ and the compatibility proxies one folder up.
   The launcher starts the engine with that folder as the working directory,
   but the engine later changes its current directory (e.g. to the SWF's
   folder) and then delay-loads further proxies (F32U, F32G, ...). On XP and
   later those loads then fail (exception C06D007E) and the player closes.
   Entry point of F32K.DLL: before the normal DllMain, prepend the folder that
   holds this DLL to the process PATH, which LoadLibrary always searches. */
extern BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID);
static void add_own_folder_to_path(HINSTANCE self){char dir[MAX_PATH],*old,*buf;DWORD n,k,i,len;
 n=GetModuleFileNameA(self,dir,MAX_PATH);if(!n||n>=MAX_PATH)return;
 while(n&&dir[n-1]!='\\')n--;if(n<3)return;dir[n-1]=0;n--;
 k=GetEnvironmentVariableA("PATH",0,0);
 buf=(char*)HeapAlloc(GetProcessHeap(),0,n+2+k+1);if(!buf)return;
 for(i=0;i<n;i++)buf[i]=dir[i];len=n;
 if(k){old=buf+len+1;if(GetEnvironmentVariableA("PATH",old,k)){
   /* already first in PATH? */
   for(i=0;i<n&&old[i]&&(old[i]|32)==(dir[i]|32);i++){}
   if(i==n&&(old[n]==';'||!old[n])){HeapFree(GetProcessHeap(),0,buf);return;}
   buf[len]=';';}else buf[len]=0;}
 else buf[len]=0;
 SetEnvironmentVariableA("PATH",buf);HeapFree(GetProcessHeap(),0,buf);}
/* Windows 95/98/ME: load FP9X.DLL (the Unicode layer) from this folder.
   It patches the imports of the player and its proxies; on Windows NT it
   is never loaded. */
static void load_9x_layer(HINSTANCE self){char p[MAX_PATH];DWORD n;const char*s="FP9X.DLL";
 if(!(GetVersion()&0x80000000u))return;
 n=GetModuleFileNameA(self,p,MAX_PATH);if(!n||n>=MAX_PATH-9)return;
 while(n&&p[n-1]!='\\')n--;while(*s)p[n++]=*s++;p[n]=0;LoadLibraryA(p);}
BOOL WINAPI FpK32Entry(HINSTANCE self,DWORD reason,LPVOID reserved){BOOL r;
 if(reason==DLL_PROCESS_ATTACH)add_own_folder_to_path(self);
 r=DllMain(self,reason,reserved);
 if(reason==DLL_PROCESS_ATTACH&&r)load_9x_layer(self);
 return r;}
