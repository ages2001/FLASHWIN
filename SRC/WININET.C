#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
/* wininet HTTP gap closures. A real HTTP client is out of scope for early NT;
   these fail cleanly so online/HTTP features degrade to "no connectivity"
   instead of aborting the player. NON-FUNCTIONAL by design. No CRT. */

HINTERNET WINAPI fp_InternetOpenA(LPCSTR agent,DWORD at,LPCSTR proxy,LPCSTR bypass,DWORD f){
    (void)agent;(void)at;(void)proxy;(void)bypass;(void)f;
    SetLastError(ERROR_INTERNET_CANNOT_CONNECT); return 0;
}
HINTERNET WINAPI fp_InternetConnectA(HINTERNET h,LPCSTR host,INTERNET_PORT port,LPCSTR user,
                                     LPCSTR pass,DWORD svc,DWORD f,DWORD_PTR ctx){
    (void)h;(void)host;(void)port;(void)user;(void)pass;(void)svc;(void)f;(void)ctx;
    SetLastError(ERROR_INTERNET_CANNOT_CONNECT); return 0;
}
HINTERNET WINAPI fp_HttpOpenRequestA(HINTERNET h,LPCSTR verb,LPCSTR obj,LPCSTR ver,LPCSTR ref,
                                     LPCSTR*accept,DWORD f,DWORD_PTR ctx){
    (void)h;(void)verb;(void)obj;(void)ver;(void)ref;(void)accept;(void)f;(void)ctx;
    SetLastError(ERROR_INTERNET_CANNOT_CONNECT); return 0;
}
BOOL WINAPI fp_HttpSendRequestA(HINTERNET h,LPCSTR hdr,DWORD hl,LPVOID opt,DWORD ol){
    (void)h;(void)hdr;(void)hl;(void)opt;(void)ol; SetLastError(ERROR_INTERNET_CANNOT_CONNECT); return FALSE;
}
BOOL WINAPI fp_HttpQueryInfoA(HINTERNET h,DWORD lvl,LPVOID buf,LPDWORD bl,LPDWORD idx){
    (void)h;(void)lvl;(void)buf; if(bl)*bl=0;(void)idx; SetLastError(ERROR_INTERNET_CANNOT_CONNECT); return FALSE;
}
BOOL WINAPI fp_InternetReadFile(HINTERNET h,LPVOID buf,DWORD n,LPDWORD read){
    (void)h;(void)buf;(void)n; if(read)*read=0; return FALSE;
}
BOOL WINAPI fp_InternetCloseHandle(HINTERNET h){(void)h;return TRUE;}
