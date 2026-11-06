#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct ClassMeta{struct ClassMeta*next;HINSTANCE inst;ATOM atom;HICON small;WCHAR name[256];}ClassMeta;
static ClassMeta*classes;static volatile LONG class_lock;
static void lock_classes(void){while(InterlockedExchange(&class_lock,1))Sleep(0);}
static void unlock_classes(void){InterlockedExchange(&class_lock,0);}
static int eqname(LPCWSTR a,LPCWSTR b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
extern void WINAPI FpObserve(const char*,DWORD);
static WNDPROC shock_proc;
static LRESULT CALLBACK shock_window(HWND w,UINT m,WPARAM a,LPARAM b){LRESULT r;if(m>=WM_MOUSEMOVE&&m<=WM_MBUTTONDBLCLK){FpObserve("Window mouse message",m);FpObserve("Window mouse coords",b);}r=CallWindowProcW(shock_proc,w,m,a,b);return r;}
BOOL WINAPI fp_SetMenu(HWND w,HMENU menu){
#ifndef FP_NATIVE_MENU
 WCHAR name[64];if(!menu&&GetMenu(w)&&GetClassNameW(w,name,64)&&eqname(name,L"ShockwaveFlash"))return TRUE;
#endif
 return SetMenu(w,menu);
}
ATOM WINAPI fp_RegisterClassExW(const WNDCLASSEXW*x){WNDCLASSW c;ATOM a;ClassMeta*m;int i;if(!x||x->cbSize!=sizeof(*x)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}c.style=x->style;c.lpfnWndProc=x->lpfnWndProc;
#ifdef FP_WINDOW_CREATE_DIAGNOSTICS
FpObserve("RegisterClassExW procedure",(DWORD)x->lpfnWndProc);
#endif
if(!IS_INTRESOURCE(x->lpszClassName)&&eqname(x->lpszClassName,L"ShockwaveFlash")){shock_proc=x->lpfnWndProc;c.lpfnWndProc=shock_window;}c.cbClsExtra=x->cbClsExtra;c.cbWndExtra=x->cbWndExtra;c.hInstance=x->hInstance;c.hIcon=x->hIcon;c.hCursor=x->hCursor;c.hbrBackground=x->hbrBackground;c.lpszMenuName=x->lpszMenuName;c.lpszClassName=x->lpszClassName;a=RegisterClassW(&c);if(!a)return 0;m=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*m));if(m){m->inst=x->hInstance;m->atom=a;m->small=x->hIconSm;if(!IS_INTRESOURCE(x->lpszClassName)){for(i=0;i<255&&x->lpszClassName[i];i++)m->name[i]=x->lpszClassName[i];m->name[i]=0;}lock_classes();m->next=classes;classes=m;unlock_classes();}return a;}
BOOL WINAPI fp_GetClassInfoExW(HINSTANCE inst,LPCWSTR name,LPWNDCLASSEXW out){WNDCLASSW c;ClassMeta*m;if(!out){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}if(!GetClassInfoW(inst,name,&c))return FALSE;out->cbSize=sizeof(*out);out->style=c.style;out->lpfnWndProc=c.lpfnWndProc==shock_window?shock_proc:c.lpfnWndProc;out->cbClsExtra=c.cbClsExtra;out->cbWndExtra=c.cbWndExtra;out->hInstance=c.hInstance;out->hIcon=c.hIcon;out->hCursor=c.hCursor;out->hbrBackground=c.hbrBackground;out->lpszMenuName=c.lpszMenuName;out->lpszClassName=c.lpszClassName;out->hIconSm=c.hIcon;lock_classes();for(m=classes;m;m=m->next)if(m->inst==inst&&((IS_INTRESOURCE(name)&&m->atom==(ATOM)(ULONG_PTR)name)||(!IS_INTRESOURCE(name)&&eqname(name,m->name)))){out->hIconSm=m->small;break;}unlock_classes();return TRUE;}
HMONITOR WINAPI fp_MonitorFromWindow(HWND w,DWORD flags){RECT r;int width=GetSystemMetrics(SM_CXSCREEN),height=GetSystemMetrics(SM_CYSCREEN);if(GetWindowRect(w,&r)&&r.right>0&&r.bottom>0&&r.left<width&&r.top<height)return (HMONITOR)1;return flags&(MONITOR_DEFAULTTOPRIMARY|MONITOR_DEFAULTTONEAREST)?(HMONITOR)1:0;}
BOOL WINAPI fp_GetMonitorInfoW(HMONITOR m,LPMONITORINFO out){RECT r;if(m!=(HMONITOR)1||!out||(out->cbSize!=sizeof(MONITORINFO)&&out->cbSize!=sizeof(MONITORINFOEXW))){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}r.left=r.top=0;r.right=GetSystemMetrics(SM_CXSCREEN);r.bottom=GetSystemMetrics(SM_CYSCREEN);out->rcMonitor=r;out->rcWork=r;out->dwFlags=MONITORINFOF_PRIMARY;if(out->cbSize==sizeof(MONITORINFOEXW)){MONITORINFOEXW*x=(void*)out;const WCHAR*s=L"\\\\.\\DISPLAY1";int i=0;while(s[i]){x->szDevice[i]=s[i];i++;}x->szDevice[i]=0;}return TRUE;}
typedef struct MouseTrack{struct MouseTrack*next;HWND hwnd;DWORD flags,hover,start;POINT anchor;UINT_PTR timer;}MouseTrack;
static MouseTrack*tracking;
static void remove_track(MouseTrack*t){MouseTrack**p;KillTimer(t->hwnd,t->timer);for(p=&tracking;*p;p=&(*p)->next)if(*p==t){*p=t->next;break;}HeapFree(GetProcessHeap(),0,t);}
static void sweep_tracks(void){MouseTrack*t=tracking,*next;while(t){next=t->next;if(!IsWindow(t->hwnd))remove_track(t);t=next;}}
static void CALLBACK mouse_timer(HWND hwnd,UINT message,UINT_PTR id,DWORD tick){
 MouseTrack*t;POINT p,local;RECT rect,outer;BOOL inside,geometry;HWND hit;UINT keys=0,post=0;LPARAM coords=0;BOOL nc=FALSE;
 /* WindowFromPoint can synchronously invoke WM_NCHITTEST. The application
    may call TrackMouseEvent there, so do not hold class_lock across User32
    geometry queries or preserve a tracking pointer across those callbacks. */
 p.x=p.y=0;geometry=GetCursorPos(&p);local=p;
 geometry=geometry&&ScreenToClient(hwnd,&local)&&GetClientRect(hwnd,&rect)&&GetWindowRect(hwnd,&outer);
 hit=geometry?WindowFromPoint(p):0;
 lock_classes();sweep_tracks();for(t=tracking;t;t=t->next)if(t->hwnd==hwnd&&t->timer==id)break;
 if(!t){unlock_classes();return;}
 nc=(t->flags&TME_NONCLIENT)!=0;inside=geometry&&(nc?(PtInRect(&outer,p)&&!PtInRect(&rect,local)):PtInRect(&rect,local));
 if(hit!=hwnd)inside=FALSE;
 if(!inside){if(t->flags&TME_LEAVE)post=nc?WM_NCMOUSELEAVE:WM_MOUSELEAVE;remove_track(t);}
 else if(t->flags&TME_HOVER){
  if(p.x-t->anchor.x>2||p.x-t->anchor.x<-2||p.y-t->anchor.y>2||p.y-t->anchor.y<-2){t->anchor=p;t->start=tick;}
  if(tick-t->start>=t->hover){
   if(GetKeyState(VK_LBUTTON)<0)keys|=MK_LBUTTON;if(GetKeyState(VK_RBUTTON)<0)keys|=MK_RBUTTON;
   if(GetKeyState(VK_MBUTTON)<0)keys|=MK_MBUTTON;if(GetKeyState(VK_SHIFT)<0)keys|=MK_SHIFT;if(GetKeyState(VK_CONTROL)<0)keys|=MK_CONTROL;
   post=nc?WM_NCMOUSEHOVER:WM_MOUSEHOVER;coords=nc?MAKELPARAM(p.x,p.y):MAKELPARAM(local.x,local.y);
   t->flags&=~TME_HOVER;if(!(t->flags&TME_LEAVE))remove_track(t);
  }
 }
 unlock_classes();
 /* Calling the window procedure can reenter TrackMouseEvent: never hold the lock. */
 if(post==WM_NCMOUSEHOVER)keys=(UINT)SendMessageW(hwnd,WM_NCHITTEST,0,coords);
 if(post)PostMessageW(hwnd,post,keys,coords);
}
BOOL WINAPI fp_TrackMouseEvent(LPTRACKMOUSEEVENT e){MouseTrack*t;DWORD flags;if(!e||e->cbSize!=sizeof(*e)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}if(!IsWindow(e->hwndTrack)||GetWindowThreadProcessId(e->hwndTrack,0)!=GetCurrentThreadId()){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}lock_classes();sweep_tracks();for(t=tracking;t;t=t->next)if(t->hwnd==e->hwndTrack)break;if(e->dwFlags&TME_QUERY){e->dwFlags=t?t->flags:0;e->dwHoverTime=t?t->hover:0;unlock_classes();return TRUE;}flags=e->dwFlags&(TME_HOVER|TME_LEAVE|TME_NONCLIENT);if(e->dwFlags&TME_CANCEL){if(t){t->flags&=~(flags&(TME_HOVER|TME_LEAVE));if(!(t->flags&(TME_HOVER|TME_LEAVE)))remove_track(t);}unlock_classes();return TRUE;}if(!(flags&(TME_HOVER|TME_LEAVE))){unlock_classes();SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}if(!t){t=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*t));if(!t){unlock_classes();return FALSE;}t->hwnd=e->hwndTrack;t->timer=SetTimer(t->hwnd,(UINT_PTR)t,50,mouse_timer);if(!t->timer){HeapFree(GetProcessHeap(),0,t);unlock_classes();return FALSE;}t->next=tracking;tracking=t;}t->flags|=flags;t->hover=e->dwHoverTime==HOVER_DEFAULT?400:e->dwHoverTime;t->start=GetTickCount();GetCursorPos(&t->anchor);unlock_classes();return TRUE;}

/* Flash queries QS_INPUT = 0x407, which includes the later QS_RAWINPUT bit.
   NT 3.1 rejects that bit with ERROR_INVALID_FLAGS instead of reporting the
   available keyboard/mouse input. This OS has no raw-input queue to report. */
DWORD WINAPI fp_GetQueueStatus(UINT flags){return GetQueueStatus(flags&~0x400u);}
/* Emulate the input-only PeekMessage queue mask used by the original Flash
   pump. Ordinary unfiltered calls retain native posted-message ordering.
   Other queue-category masks still go to native validation; this is not a
   general implementation of every later Windows queue category. */
static BOOL input_range(LPMSG m,HWND w,UINT lo,UINT hi,UINT first,UINT last,UINT flags){
 if(lo||hi){if(lo>first)first=lo;if(hi<last)last=hi;}
 return first<=last?PeekMessageW(m,w,first,last,flags):FALSE;
}
static BOOL compatible_peek(LPMSG m,HWND w,UINT lo,UINT hi,UINT flags){
 UINT mask=flags>>16,plain=flags&3u;DWORD queued;MSG key,mouse;BOOL k,b;
 if((mask!=0x407&&mask!=7)||(flags&0xfffcu))return PeekMessageW(m,w,lo,hi,flags);
 /* WM_QUIT is returned regardless of the numeric message filter. */
 if(PeekMessageW(&key,w,WM_QUIT,WM_QUIT,plain&~PM_REMOVE)){
  if(plain&PM_REMOVE)return PeekMessageW(m,w,WM_QUIT,WM_QUIT,plain);
  *m=key;return TRUE;
 }
 queued=GetQueueStatus(7)>>16;
 k=(queued&QS_KEY)&&input_range(&key,w,lo,hi,WM_KEYFIRST,WM_KEYLAST,plain&~PM_REMOVE);
 b=(queued&QS_MOUSE)&&input_range(&mouse,w,lo,hi,WM_MOUSEFIRST,WM_MOUSELAST,plain&~PM_REMOVE);
 if(!k&&!b)return FALSE;
 if(k&&(!b||key.message==WM_QUIT||(LONG)(key.time-mouse.time)<=0)){
  if(plain&PM_REMOVE)return input_range(m,w,lo,hi,WM_KEYFIRST,WM_KEYLAST,plain);
  *m=key;
 }else{
  if(plain&PM_REMOVE)return input_range(m,w,lo,hi,WM_MOUSEFIRST,WM_MOUSELAST,plain);
  *m=mouse;
 }
 return TRUE;
}

/* Diagnostic-only experiment: check whether posted frames starve native input.
   Disabled in ordinary builds; explicit message filters remain unchanged. */
#ifdef FP_INPUT_PRIORITY_PROBE
static BOOL pending_input(LPMSG m,HWND w,UINT flags){
 if(PeekMessageW(m,w,WM_KEYFIRST,WM_KEYLAST,flags))return TRUE;
 return PeekMessageW(m,w,WM_MOUSEFIRST,WM_MOUSELAST,flags);
}
BOOL WINAPI fp_GetMessageW(LPMSG m,HWND w,UINT lo,UINT hi){
 if(!lo&&!hi&&pending_input(m,w,PM_REMOVE))return m->message!=WM_QUIT;
 return GetMessageW(m,w,lo,hi);
}
static BOOL probe_peek(LPMSG m,HWND w,UINT lo,UINT hi,UINT flags){
 if(!lo&&!hi&&pending_input(m,w,flags))return TRUE;
 return compatible_peek(m,w,lo,hi,flags);
}
#define compatible_peek probe_peek
#endif
/* Temporary diagnostic taps retain native behavior and return values. */
extern void WINAPI FpObserve(const char*,DWORD);
static LRESULT CALLBACK observed_window(HWND w,UINT m,WPARAM a,LPARAM b){WNDPROC old=(WNDPROC)GetPropA(w,"FP10DiagnosticWindowProc");LRESULT r;if(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP){FpObserve("Observed mouse",m);FpObserve("Observed coords",b);FpObserve("Observed hwnd",(DWORD)w);}r=old?CallWindowProcW(old,w,m,a,b):DefWindowProcW(w,m,a,b);if(m==WM_NCDESTROY)RemovePropA(w,"FP10DiagnosticWindowProc");return r;}
HWND WINAPI fp_CreateWindowExW(DWORD ex,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE inst,LPVOID param){HWND result;
#ifdef FP_WINDOW_CREATE_DIAGNOSTICS
{WNDCLASSW c;char name[160];int n=0;if(!IS_INTRESOURCE(cls)){while(cls[n]&&n<159){name[n]=cls[n]<128?(char)cls[n]:'?';n++;}name[n]=0;FpObserve(name,(DWORD)inst);}FpObserve("CreateWindow parent",(DWORD)parent);FpObserve("CreateWindow style",style);if(GetClassInfoW(inst,cls,&c))FpObserve("Class procedure before create",(DWORD)c.lpfnWndProc);}
#endif
result=CreateWindowExW(ex,cls,title,style,x,y,w,h,parent,menu,inst,param);DWORD error=GetLastError();if(result){WCHAR actual[64];GetClassNameW(result,actual,64);if(eqname(actual,L"ShockwaveFlash")){WNDPROC old=(WNDPROC)GetWindowLongW(result,GWL_WNDPROC);if(SetPropA(result,"FP10DiagnosticWindowProc",(HANDLE)old))SetWindowLongW(result,GWL_WNDPROC,(LONG)observed_window);}}FpObserve("CreateWindowExW hwnd",(DWORD)result);FpObserve("CreateWindowExW error",error);SetLastError(error);return result;}
LRESULT WINAPI fp_DispatchMessageW(const MSG*m){static DWORD count;LRESULT r;DWORD e;if(count++<80||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){FpObserve("DispatchMessageW message",m->message);FpObserve("DispatchMessageW lParam",m->lParam);FpObserve("DispatchMessageW wParam",m->wParam);}r=DispatchMessageW(m);e=GetLastError();if(count<81)FpObserve("DispatchMessageW result",r);SetLastError(e);return r;}
BOOL WINAPI fp_PeekMessageW(LPMSG m,HWND w,UINT lo,UINT hi,UINT flags){static DWORD count,extended_count;BOOL r=compatible_peek(m,w,lo,hi,flags);DWORD e=GetLastError();if((flags&~3u)&&extended_count++<100){FpObserve("PeekMessage extended flags",flags);FpObserve("PeekMessage return",r);FpObserve("PeekMessage error",e);}if(r&&(m->message==WM_LBUTTONDOWN||m->message==WM_LBUTTONUP)){FpObserve("Peek mouse",m->message);FpObserve("Peek coords",m->lParam);FpObserve("Peek hwnd",(DWORD)m->hwnd);}if(r&&count++<80)FpObserve("PeekMessageW message",m->message);SetLastError(e);return r;}
static const BYTE* english_resource(HINSTANCE m,LPCWSTR type,LPCWSTR name,DWORD*size){HRSRC r=FindResourceExW(m,type,name,1033);HGLOBAL h;if(!r){FpObserve("English resource lookup error",GetLastError());return 0;}*size=SizeofResource(m,r);h=LoadResource(m,r);if(!h){FpObserve("English resource load error",GetLastError());return 0;}return LockResource(h);}
HMENU WINAPI fp_LoadMenuW(HINSTANCE m,LPCWSTR name){DWORD size;const BYTE*p=english_resource(m,MAKEINTRESOURCEW(4),name,&size);HMENU h=p&&size>=4?LoadMenuIndirectW((const MENUTEMPLATEW*)p):0;DWORD e=GetLastError();FpObserve("LoadMenuW id",(DWORD)name);FpObserve("LoadMenuW result",(DWORD)h);if(!h)FpObserve("LoadMenuW error",e);SetLastError(e);return h;}
int WINAPI fp_LoadStringW(HINSTANCE m,UINT id,LPWSTR out,int cap){DWORD bytes;const WORD*p=(const WORD*)english_resource(m,MAKEINTRESOURCEW(6),MAKEINTRESOURCEW((id>>4)+1),&bytes);const WORD*end;UINT i,n;if(!p||cap<0||!out)return 0;end=p+bytes/2;for(i=0;i<(id&15);i++){if(p>=end||p+1+*p>end)return 0;p+=1+*p;}if(p>=end||p+1+*p>end)return 0;n=*p++;if(cap==0){*(const WCHAR**)out=(const WCHAR*)p;return n;}if(n>=(UINT)cap)n=cap-1;for(i=0;i<n;i++)out[i]=p[i];out[n]=0;return n;}
#ifndef FP_CPU_MODE_LABEL
#define FP_CPU_MODE_LABEL L"non-SSE2"
#endif
#ifdef FP_DYNAMIC_CPU
#include "fp10_cpu_mode.h"
static INT_PTR fp10_dynamic_about(HWND parent) {
    const WCHAR *sse=L"Flash Player 10.3.183.90\r\nSSE2\r\n\r\n"
        L"Ported to Windows NT 3.1/3.5x/4/2000/XP and Windows 95/98/ME\r\n"
        L"Ported by ages2001\r\n\r\n"
        L"Original Flash Player: Copyright 1996-2011 Adobe Systems Incorporated.";
    const WCHAR *scalar=L"Flash Player 10.3.183.90\r\nnon-SSE2\r\n\r\n"
        L"Ported to Windows NT 3.1/3.5x/4/2000/XP and Windows 95/98/ME\r\n"
        L"Ported by ages2001\r\n\r\n"
        L"Original Flash Player: Copyright 1996-2011 Adobe Systems Incorporated.";
    return MessageBoxW(parent,fp10_selected_sse2()?sse:scalar,L"About Flash Player 10",MB_OK);
}
#endif
INT_PTR WINAPI fp_DialogBoxParamW(HINSTANCE m,LPCWSTR name,HWND parent,DLGPROC proc,LPARAM param){DWORD size;const BYTE*p;
#ifdef FP_DYNAMIC_CPU
if(IS_INTRESOURCE(name)&&(ULONG_PTR)name==401)return fp10_dynamic_about(parent);
#endif
if(IS_INTRESOURCE(name)&&(ULONG_PTR)name==401)return MessageBoxW(parent,L"Flash Player 10.3.183.90\r\n" FP_CPU_MODE_LABEL L"\r\n\r\nPorted to Windows NT 3.1/3.5x/4/2000/XP and Windows 95/98/ME\r\nPorted by ages2001\r\n\r\nOriginal Flash Player: Copyright 1996-2011 Adobe Systems Incorporated.",L"About Flash Player 10",MB_OK);p=english_resource(m,MAKEINTRESOURCEW(5),name,&size);if(!p||size<18)return -1;return DialogBoxIndirectParamW(m,(const DLGTEMPLATE*)p,parent,proc,param);}
HWND WINAPI fp_CreateDialogParamW(HINSTANCE m,LPCWSTR name,HWND parent,DLGPROC proc,LPARAM param){DWORD size;const BYTE*p=english_resource(m,MAKEINTRESOURCEW(5),name,&size);if(!p||size<18)return 0;return CreateDialogIndirectParamW(m,(const DLGTEMPLATE*)p,parent,proc,param);}

INT_PTR WINAPI fp_DialogBoxIndirectParamW(HINSTANCE m,const DLGTEMPLATE*t,HWND parent,DLGPROC proc,LPARAM param){DWORD size;const BYTE*about=english_resource(m,MAKEINTRESOURCEW(5),MAKEINTRESOURCEW(401),&size);FpObserve("DialogBoxIndirect template",(DWORD)t);if((const BYTE*)t==about)return fp_DialogBoxParamW(m,MAKEINTRESOURCEW(401),parent,proc,param);return DialogBoxIndirectParamW(m,t,parent,proc,param);}
