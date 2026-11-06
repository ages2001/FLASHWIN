#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
/* ws2_32 gap closures. wsock32 (the classic WinSock) is loaded DYNAMICALLY so
   this proxy carries NO static WSOCK32.DLL import: it therefore loads on hosts
   that lack WinSock entirely (e.g. Windows 3.1x + Win32s), where the socket
   calls then fail cleanly, while on NT (wsock32 present) they work for real.
   Byte-order helpers are implemented inline. No CRT. */

static FARPROC wsk(const char*name){
    static HMODULE h; static int tried;
    if(!tried){ tried=1; h=GetModuleHandleA("wsock32.dll"); if(!h)h=LoadLibraryA("wsock32.dll"); }
    return h?GetProcAddress(h,name):0;
}
static int a_to_i(const char*s){ int v=0; if(!s)return 0; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} return v; }
static void i_to_a(unsigned v,char*o){ char t[12]; int n=0; if(!v){o[0]='0';o[1]=0;return;} while(v){t[n++]='0'+v%10;v/=10;} { int i; for(i=0;i<n;i++)o[i]=t[n-1-i]; o[n]=0; } }
static void s_cat(char*d,const char*s){ while(*d)d++; while(*s)*d++=*s++; *d=0; }

/* inline byte-order (no wsock32 needed) */
static unsigned short bswap16(unsigned short x){ return (unsigned short)((x>>8)|(x<<8)); }
static unsigned long  bswap32(unsigned long x){ return (x>>24)|((x>>8)&0xFF00)|((x<<8)&0xFF0000)|(x<<24); }
static unsigned short h_htons(unsigned short x){ return bswap16(x); }
static unsigned short h_ntohs(unsigned short x){ return bswap16(x); }
static unsigned long  h_htonl(unsigned long x){ return bswap32(x); }

/* dynamic wsock32 wrappers */
static SOCKET d_socket(int af,int t,int p){ SOCKET(WINAPI*f)(int,int,int)=(SOCKET(WINAPI*)(int,int,int))wsk("socket"); return f?f(af,t,p):(SOCKET)INVALID_SOCKET; }
static struct hostent* d_gethostbyname(const char*n){ struct hostent*(WINAPI*f)(const char*)=(struct hostent*(WINAPI*)(const char*))wsk("gethostbyname"); return f?f(n):0; }
static unsigned long d_inet_addr(const char*c){ unsigned long(WINAPI*f)(const char*)=(unsigned long(WINAPI*)(const char*))wsk("inet_addr"); return f?f(c):INADDR_NONE; }
static char* d_inet_ntoa(struct in_addr a){ char*(WINAPI*f)(struct in_addr)=(char*(WINAPI*)(struct in_addr))wsk("inet_ntoa"); return f?f(a):0; }
static void d_setlasterr(int e){ void(WINAPI*f)(int)=(void(WINAPI*)(int))wsk("WSASetLastError"); if(f)f(e); else SetLastError((DWORD)e); }

SOCKET WINAPI fp_WSASocketA(int af,int type,int proto,void*pi,unsigned g,DWORD flags){
    (void)pi;(void)g;(void)flags; return d_socket(af,type,proto);
}
SOCKET WINAPI fp_WSASocketW(int af,int type,int proto,void*pi,unsigned g,DWORD flags){
    (void)pi;(void)g;(void)flags; return d_socket(af,type,proto);
}

int WINAPI fp_getaddrinfo(const char*node,const char*service,const struct addrinfo*hints,struct addrinfo**res){
    struct addrinfo*ai; struct sockaddr_in*sa; unsigned long addr=INADDR_NONE;
    if(!res) return EAI_FAIL; *res=0;
    if(node){
        addr=d_inet_addr(node);
        if(addr==INADDR_NONE){
            struct hostent*he=d_gethostbyname(node);
            if(he&&he->h_addr_list[0]) addr=*(unsigned long*)he->h_addr_list[0];
            else return EAI_NONAME;
        }
    } else addr=h_htonl(INADDR_LOOPBACK);
    ai=(struct addrinfo*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*ai)+sizeof(*sa));
    if(!ai) return EAI_MEMORY;
    sa=(struct sockaddr_in*)(ai+1);
    sa->sin_family=AF_INET; sa->sin_addr.s_addr=addr;
    sa->sin_port=h_htons((unsigned short)a_to_i(service));
    ai->ai_family=AF_INET;
    ai->ai_socktype=hints?hints->ai_socktype:SOCK_STREAM;
    ai->ai_protocol=hints?hints->ai_protocol:0;
    ai->ai_addrlen=sizeof(*sa); ai->ai_addr=(struct sockaddr*)sa; ai->ai_next=0;
    *res=ai; return 0;
}
void WINAPI fp_freeaddrinfo(struct addrinfo*ai){
    while(ai){ struct addrinfo*n=ai->ai_next; HeapFree(GetProcessHeap(),0,ai); ai=n; }
}
int WINAPI fp_getnameinfo(const struct sockaddr*sa,socklen_t sl,char*host,DWORD hl,char*serv,DWORD svl,int flags){
    const struct sockaddr_in*in=(const struct sockaddr_in*)sa; (void)sl;(void)flags;
    if(!in) return EAI_FAIL;
    if(host&&hl){ char*ip=d_inet_ntoa(in->sin_addr); DWORD i=0; if(ip){ while(ip[i]&&i<hl-1){host[i]=ip[i];i++;} } host[i]=0; }
    if(serv&&svl){ i_to_a(h_ntohs(in->sin_port),serv); }
    return 0;
}
int WINAPI fp_WSAAddressToStringA(struct sockaddr*sa,DWORD sal,void*pi,char*buf,DWORD*len){
    struct sockaddr_in*in=(struct sockaddr_in*)sa; char tmp[32]; char*ip; DWORD n;
    (void)sal;(void)pi;
    if(!in||!buf||!len) return SOCKET_ERROR;
    ip=d_inet_ntoa(in->sin_addr); tmp[0]=0;
    if(ip) s_cat(tmp,ip);
    if(in->sin_port){ char p[8]; s_cat(tmp,":"); i_to_a(h_ntohs(in->sin_port),p); s_cat(tmp,p); }
    { DWORD i=0; while(tmp[i])i++; n=i+1; }
    if(*len<n){ *len=n; d_setlasterr(WSAEFAULT); return SOCKET_ERROR; }
    { DWORD i; for(i=0;i<n;i++) buf[i]=tmp[i]; } *len=n; return 0;
}

/* Winsock-2 async event model: not available on early NT / Win32s. */
int WINAPI fp_WSAEventSelect(SOCKET s,WSAEVENT e,long ev){(void)s;(void)e;(void)ev;d_setlasterr(WSAEINVAL);return SOCKET_ERROR;}
int WINAPI fp_WSAEnumNetworkEvents(SOCKET s,WSAEVENT e,LPWSANETWORKEVENTS ne){(void)s;(void)e;if(ne){ne->lNetworkEvents=0;}d_setlasterr(WSAEINVAL);return SOCKET_ERROR;}
int WINAPI fp_WSAIoctl(SOCKET s,DWORD code,LPVOID inb,DWORD inl,LPVOID outb,DWORD outl,LPDWORD ret,LPVOID ov,LPVOID cr){
    (void)s;(void)code;(void)inb;(void)inl;(void)outb;(void)outl;(void)ov;(void)cr; if(ret)*ret=0; d_setlasterr(WSAEOPNOTSUPP); return SOCKET_ERROR;
}
