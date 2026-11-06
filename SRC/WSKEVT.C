/* Winsock event objects via native manual-reset events.  WSASetLastError is
   resolved dynamically from wsock32 (falling back to SetLastError) so this
   object carries NO static WSOCK32.DLL import and loads where WinSock is absent
   (e.g. Windows 3.1x + Win32s). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

static void set_wsa_error(int e){
    static HMODULE h; static int tried; void(WINAPI*f)(int);
    if(!tried){ tried=1; h=GetModuleHandleA("wsock32.dll"); if(!h)h=LoadLibraryA("wsock32.dll"); }
    f=(void(WINAPI*)(int))(h?GetProcAddress(h,"WSASetLastError"):0);
    if(f)f(e); else SetLastError((DWORD)e);
}

WSAEVENT WINAPI fp_WSACreateEvent(void)
{
    HANDLE event=CreateEventA(0,TRUE,FALSE,0);
    if(!event){
        DWORD error=GetLastError();
        set_wsa_error(error==ERROR_OUTOFMEMORY?WSA_NOT_ENOUGH_MEMORY:(int)error);
    }
    return event;
}

BOOL WINAPI fp_WSACloseEvent(WSAEVENT event)
{
    if(CloseHandle(event))return TRUE;
    set_wsa_error((int)GetLastError());
    return FALSE;
}
