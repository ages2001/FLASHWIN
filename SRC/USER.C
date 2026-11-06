#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* user32 gap closures for early NT. Map the modern APIs onto the classic ones
   that NT 3.1/3.5x export. No CRT, no SIMD. */

BOOL WINAPI fp_ShowWindowAsync(HWND hwnd,int cmd){ return ShowWindow(hwnd,cmd); }

BOOL WINAPI fp_FlashWindowEx(PFLASHWINFO pfwi){
    if(!pfwi||!pfwi->hwnd)return FALSE;
    /* FLASHW_STOP (0) clears; any flag flashes once. */
    return FlashWindow(pfwi->hwnd, pfwi->dwFlags?TRUE:FALSE);
}

ATOM WINAPI fp_RegisterClassExA(const WNDCLASSEXA*ex){
    WNDCLASSA c;
    if(!ex)return 0;
    c.style=ex->style; c.lpfnWndProc=ex->lpfnWndProc;
    c.cbClsExtra=ex->cbClsExtra; c.cbWndExtra=ex->cbWndExtra;
    c.hInstance=ex->hInstance; c.hIcon=ex->hIcon; c.hCursor=ex->hCursor;
    c.hbrBackground=ex->hbrBackground; c.lpszMenuName=ex->lpszMenuName;
    c.lpszClassName=ex->lpszClassName;
    /* hIconSm (ex->hIconSm) has no classic equivalent and is dropped. */
    return RegisterClassA(&c);
}

/* No per-thread keyboard-layout handle tracking on early NT; report US English. */
HKL WINAPI fp_GetKeyboardLayout(DWORD thread){(void)thread;return (HKL)(ULONG_PTR)0x04090409;}

/* EnumDisplaySettings is not available on NT 3.1 (no EnumDisplaySettingsA to
   forward to). Report "no mode" so the Flash presenter falls back to GetDeviceCaps
   for the current resolution/depth. */
BOOL WINAPI fp_EnumDisplaySettingsW(LPCWSTR dev,DWORD mode,DEVMODEW*dmw){
    (void)dev;(void)mode;(void)dmw; return FALSE;
}

/* No user-object info on early NT; report "not available" cleanly. */
BOOL WINAPI fp_GetUserObjectInformationW(HANDLE h,int idx,PVOID info,DWORD len,LPDWORD need){
    (void)h;(void)idx;(void)info;(void)len; if(need)*need=0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}

/* Compose GetWindowInfo from the classic queries. */
BOOL WINAPI fp_GetWindowInfo(HWND hwnd,PWINDOWINFO pwi){
    if(!hwnd||!pwi||pwi->cbSize<sizeof(WINDOWINFO))return FALSE;
    if(!GetWindowRect(hwnd,&pwi->rcWindow))return FALSE;
    GetClientRect(hwnd,&pwi->rcClient);
    { POINT tl; tl.x=pwi->rcClient.left; tl.y=pwi->rcClient.top; ClientToScreen(hwnd,&tl);
      pwi->rcClient.right+=tl.x-pwi->rcClient.left; pwi->rcClient.bottom+=tl.y-pwi->rcClient.top;
      pwi->rcClient.left=tl.x; pwi->rcClient.top=tl.y; }
    pwi->dwStyle=GetWindowLongA(hwnd,GWL_STYLE);
    pwi->dwExStyle=GetWindowLongA(hwnd,GWL_EXSTYLE);
    pwi->dwWindowStatus=(GetActiveWindow()==hwnd)?WS_ACTIVECAPTION:0;
    pwi->cxWindowBorders=GetSystemMetrics(SM_CXFRAME);
    pwi->cyWindowBorders=GetSystemMetrics(SM_CYFRAME);
    pwi->atomWindowType=0; pwi->wCreatorVersion=0x0400;
    return TRUE;
}

/* Layered windows don't exist on early NT; report failure so the Flash
   presenter falls back to ordinary GDI blitting. */
BOOL WINAPI fp_UpdateLayeredWindow(HWND h,HDC dst,POINT*pd,SIZE*ps,HDC src,POINT*psrc,
                                   COLORREF key,void*blend,DWORD flags){
    (void)h;(void)dst;(void)pd;(void)ps;(void)src;(void)psrc;(void)key;(void)blend;(void)flags;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}
