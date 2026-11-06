#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* kernel32 gap closures: real cooperative fibers, event-backed waitable timers,
   thread-backed timer-queue timers. QueueUserAPC cannot be supplied on NT 3.1
   (no NtQueueApcThread) and returns a clean failure. No CRT. */

/* ---- one-time TLS slot for the current fiber ---- */
static volatile LONG g_tls_state = 0;   /* 0=unset,1=allocating,2=ready */
static DWORD g_tls;
static DWORD fiber_tls(void){
    if(InterlockedCompareExchange((LONG*)&g_tls_state,1,0)==0){
        g_tls = TlsAlloc(); g_tls_state = 2;
    } else { while(g_tls_state!=2) Sleep(0); }
    return g_tls;
}

typedef void (WINAPI *FIBERPROC)(void*);
typedef struct Fiber {
    void *esp;          /* saved stack pointer */
    void *stack;        /* VirtualAlloc base (0 for the thread-fiber) */
    FIBERPROC proc;
    void *param;
} Fiber;

extern void __stdcall fp_ctx_switch(void **save_esp, void *load_esp);
extern void fp_fiber_entry(void);   /* asm trampoline */

/* Called by the asm trampoline when a new fiber first runs. */
void fp_fiber_start(void){
    Fiber *f = (Fiber*)TlsGetValue(g_tls);
    if(f && f->proc) f->proc(f->param);
    /* A fiber proc returning has undefined behaviour in Win32; end the thread. */
    ExitThread(0);
}

LPVOID WINAPI fp_ConvertThreadToFiber(LPVOID param){
    DWORD t = fiber_tls();
    Fiber *f = (Fiber*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Fiber));
    if(!f) return 0;
    f->param = param;
    TlsSetValue(t, f);
    return f;
}
BOOL WINAPI fp_ConvertFiberToThread(void){
    DWORD t = fiber_tls();
    Fiber *f = (Fiber*)TlsGetValue(t);
    if(f){ HeapFree(GetProcessHeap(),0,f); TlsSetValue(t,0); }
    return TRUE;
}
LPVOID WINAPI fp_CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE proc, LPVOID param){
    Fiber *f; BYTE *base; void **sp; SIZE_T sz = stack? stack : (64*1024);
    fiber_tls();
    f = (Fiber*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Fiber));
    if(!f) return 0;
    base = (BYTE*)VirtualAlloc(0, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if(!base){ HeapFree(GetProcessHeap(),0,f); return 0; }
    f->stack = base; f->proc = (FIBERPROC)proc; f->param = param;
    /* Build the initial stack image that fp_ctx_switch will restore. Leave 16
       bytes of headroom at the top so the switch's `ret 8` (which raises esp by
       8 past the return address) never runs off the end of the stack:
       sp -> [edi][esi][ebx][ebp][return=fp_fiber_entry] ...16B headroom... top */
    sp = (void**)(base + sz);
    sp -= 4;                          /* 16 bytes headroom */
    *--sp = (void*)fp_fiber_entry;    /* ctx_switch 'ret' lands here */
    *--sp = (void*)0;                 /* ebp */
    *--sp = (void*)0;                 /* ebx */
    *--sp = (void*)0;                 /* esi */
    *--sp = (void*)0;                 /* edi */
    f->esp = sp;
    return f;
}
VOID WINAPI fp_SwitchToFiber(LPVOID fiber){
    DWORD t = g_tls;
    Fiber *prev = (Fiber*)TlsGetValue(t);
    Fiber *next = (Fiber*)fiber;
    if(!next || next==prev) return;
    TlsSetValue(t, next);
    fp_ctx_switch(&prev->esp, next->esp);
}
VOID WINAPI fp_DeleteFiber(LPVOID fiber){
    Fiber *f = (Fiber*)fiber;
    if(!f) return;
    if(f->stack) VirtualFree(f->stack, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(),0,f);
}

/* ---- waitable timers: a manual-reset event + a worker thread ---- */
typedef struct Timer {
    HANDLE evt;             /* the waitable object the caller holds */
    HANDLE thread;
    volatile LONG cancel;
    LONGLONG due100ns;      /* relative due time, negative per API */
    LONG period_ms;
} Timer;

/* map the event handle back to its Timer via a small table */
#define MAXT 64
static Timer* g_timers[MAXT];
static volatile LONG g_tlock = 0;
static void tlock(void){ while(InterlockedExchange((LONG*)&g_tlock,1)) Sleep(0); }
static void tunlock(void){ InterlockedExchange((LONG*)&g_tlock,0); }
static Timer* find_timer(HANDLE evt){
    int i; Timer*r=0; tlock();
    for(i=0;i<MAXT;i++) if(g_timers[i]&&g_timers[i]->evt==evt){r=g_timers[i];break;}
    tunlock(); return r;
}

static HANDLE make_timer(BOOL manual){
    Timer *t; int i;
    HANDLE e = CreateEventA(0, manual?TRUE:TRUE, FALSE, 0); /* timers are manual-reset */
    if(!e) return 0;
    t = (Timer*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Timer));
    if(!t){ CloseHandle(e); return 0; }
    t->evt = e;
    tlock();
    for(i=0;i<MAXT;i++) if(!g_timers[i]){ g_timers[i]=t; break; }
    tunlock();
    return e;   /* caller waits on the event handle */
}
HANDLE WINAPI fp_CreateWaitableTimerA(LPSECURITY_ATTRIBUTES sa,BOOL manual,LPCSTR name){
    (void)sa;(void)name; return make_timer(manual);
}
HANDLE WINAPI fp_CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa,BOOL manual,LPCWSTR name){
    (void)sa;(void)name; return make_timer(manual);
}
/* 100ns relative interval -> ms without 64-bit division (no __divdi3 in this
   freestanding build). Magnitudes above ~214s are clamped to a 32-bit count. */
static DWORD hundredns_to_ms(LONGLONG v){
    unsigned long long mag = (unsigned long long)(v<0 ? -v : v);
    unsigned long u = (mag > 0x7FFFFFFFULL) ? 0x7FFFFFFFUL : (unsigned long)mag;
    return (DWORD)(u / 10000UL);   /* 32-bit divide */
}
static DWORD WINAPI timer_worker(LPVOID p){
    Timer *t=(Timer*)p;
    LONGLONG due=t->due100ns;
    DWORD ms = (due<0)? hundredns_to_ms(due) : 0;
    for(;;){
        DWORD waited=0;
        while(waited<ms){
            if(t->cancel) return 0;
            { DWORD chunk = (ms-waited)>50?50:(ms-waited); Sleep(chunk); waited+=chunk; }
        }
        if(t->cancel) return 0;
        SetEvent(t->evt);
        if(t->period_ms<=0) return 0;
        ms = t->period_ms;
    }
}
BOOL WINAPI fp_SetWaitableTimer(HANDLE h,const LARGE_INTEGER*due,LONG period,
                                PTIMERAPCROUTINE apc,LPVOID arg,BOOL resume){
    Timer *t=find_timer(h); (void)apc;(void)arg;(void)resume;
    if(!t||!due) return FALSE;
    t->cancel=0; ResetEvent(t->evt);
    t->due100ns = due->QuadPart; t->period_ms = period;
    if(t->thread){ /* restart */ t->cancel=1; WaitForSingleObject(t->thread,200); CloseHandle(t->thread); t->cancel=0; }
    { DWORD id; t->thread = CreateThread(0,0,timer_worker,t,0,&id); }
    return t->thread!=0;
}
BOOL WINAPI fp_CancelWaitableTimer(HANDLE h){
    Timer *t=find_timer(h);
    if(!t) return FALSE;
    t->cancel=1;
    if(t->thread){ WaitForSingleObject(t->thread,200); CloseHandle(t->thread); t->thread=0; }
    return TRUE;
}

/* ---- timer-queue timers: one worker thread per timer ---- */
typedef struct TQ { HANDLE thread; volatile LONG cancel; WAITORTIMERCALLBACK cb; PVOID arg; DWORD due; DWORD period; } TQ;
static DWORD WINAPI tq_worker(LPVOID p){
    TQ*q=(TQ*)p;
    for(;;){
        DWORD waited=0, target=q->due;
        while(waited<target){ if(q->cancel)return 0; { DWORD c=(target-waited)>50?50:(target-waited); Sleep(c); waited+=c; } }
        if(q->cancel) return 0;
        if(q->cb) q->cb(q->arg, TRUE);
        if(!q->period) return 0;
        q->due=q->period;
    }
}
BOOL WINAPI fp_CreateTimerQueueTimer(PHANDLE phNew,HANDLE queue,WAITORTIMERCALLBACK cb,
                                     PVOID arg,DWORD due,DWORD period,ULONG flags){
    TQ*q; DWORD id; (void)queue;(void)flags;
    if(!phNew) return FALSE;
    q=(TQ*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(TQ));
    if(!q) return FALSE;
    q->cb=cb; q->arg=arg; q->due=due; q->period=period;
    q->thread=CreateThread(0,0,tq_worker,q,0,&id);
    if(!q->thread){ HeapFree(GetProcessHeap(),0,q); return FALSE; }
    *phNew=(HANDLE)q; return TRUE;
}
BOOL WINAPI fp_DeleteTimerQueueTimer(HANDLE queue,HANDLE timer,HANDLE event){
    TQ*q=(TQ*)timer; (void)queue;(void)event;
    if(!q) return FALSE;
    q->cancel=1;
    if(q->thread){ WaitForSingleObject(q->thread,300); CloseHandle(q->thread); }
    HeapFree(GetProcessHeap(),0,q); return TRUE;
}

/* QueueUserAPC: NT 3.1/3.51 export no NtQueueApcThread; cannot deliver a user
   APC to another thread. Fail cleanly (0) instead of aborting. Non-functional. */
DWORD WINAPI fp_QueueUserAPC(PAPCFUNC fn,HANDLE thread,ULONG_PTR data){
    (void)fn;(void)thread;(void)data; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0;
}
