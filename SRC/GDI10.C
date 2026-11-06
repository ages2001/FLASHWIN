#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* CPU-backed DIB section bridge for the NT 3.1 GDI engine. Pixel memory is
   synchronized at intercepted GDI operations. No SIMD instructions used. */
typedef struct Dib{struct Dib*next;HBITMAP bitmap,temp;HDC io,owner;void*bits,*allocation,*scratch,*shadow;BOOL shadow_valid;BITMAPINFO*info;DWORD bytes,stride,height,usage,offset;HANDLE section;}Dib;
extern void WINAPI FpObserve(const char*,DWORD);
static Dib*dibs;static CRITICAL_SECTION guard;
BOOL WINAPI fp_gdi_main(DWORD reason){if(reason==DLL_PROCESS_ATTACH)InitializeCriticalSection(&guard);return TRUE;}
static Dib*find(HGDIOBJ b){Dib*d;for(d=dibs;d;d=d->next)if(d->bitmap==b)return d;return 0;}
static Dib*current(HDC dc){return dc?find(GetCurrentObject(dc,OBJ_BITMAP)):0;}
/* Synchronize the native DDB with the CPU pixels. Once a complete shadow is
   known, only the changed scanline span needs a native SetDIBits transfer.
   NT 3.1 accepts a compact bottom-up buffer for the requested start/count. */
static int transfer(Dib*d,int upload){
 DWORD ci,first=0,last=d->height-1,start=0,lines=d->height;
 DWORD info_size,i,row,word;HGDIOBJ old=0;int n;BYTE local[1200];
 BITMAPINFO*bi=(BITMAPINFO*)local;void*data=d->bits;
 BOOL flip=d->info->bmiHeader.biHeight<0;
 BOOL cacheable=d->info->bmiHeader.biCompression==BI_RGB&&
  (d->info->bmiHeader.biBitCount==24||d->info->bmiHeader.biBitCount==32);
 if(upload&&cacheable&&d->shadow_valid){
  DWORD words=d->bytes/4,end;
  for(ci=0;ci<words;ci++)if(((DWORD*)d->bits)[ci]!=((DWORD*)d->shadow)[ci])break;
  if(ci==words)return d->height;
  first=ci/(d->stride/4);
  for(end=words;end>ci+1;end--)if(((DWORD*)d->bits)[end-1]!=((DWORD*)d->shadow)[end-1])break;
  last=(end-1)/(d->stride/4);lines=last-first+1;
  start=flip?d->height-1-last:first;
 }
 info_size=d->info->bmiHeader.biSize+
  (d->info->bmiHeader.biClrUsed?d->info->bmiHeader.biClrUsed:
   (d->info->bmiHeader.biBitCount<=8?1u<<d->info->bmiHeader.biBitCount:0))*(d->usage==DIB_PAL_COLORS?2:4);
 if(d->info->bmiHeader.biSize==40&&d->info->bmiHeader.biCompression==BI_BITFIELDS)info_size+=12;
 if(info_size>sizeof(local))return 0;
 for(i=0;i<info_size;i++)local[i]=((BYTE*)d->info)[i];
 bi->bmiHeader.biHeight=d->height;
 if(flip){
  if(!d->scratch)d->scratch=HeapAlloc(GetProcessHeap(),0,d->bytes);
  if(!d->scratch)return 0;data=d->scratch;
  if(upload)for(row=0;row<lines;row++)for(word=0;word<d->stride/4;word++)
   ((DWORD*)data)[row*(d->stride/4)+word]=((DWORD*)d->bits)[(last-row)*(d->stride/4)+word];
 }else if(upload)data=(BYTE*)d->bits+first*d->stride;
 if(d->owner&&GetCurrentObject(d->owner,OBJ_BITMAP)==d->bitmap)old=SelectObject(d->owner,d->temp);
 if(upload)n=SetDIBits(d->io,d->bitmap,start,lines,data,bi,d->usage);
 else n=GetDIBits(d->io,d->bitmap,0,d->height,data,bi,d->usage);
 if(!upload&&n&&flip)for(row=0;row<d->height;row++)for(word=0;word<d->stride/4;word++)
  ((DWORD*)d->bits)[row*(d->stride/4)+word]=((DWORD*)data)[(d->height-1-row)*(d->stride/4)+word];
 if(old&&old!=HGDI_ERROR)SelectObject(d->owner,old);
 if(cacheable&&n==(int)lines){
  if(!d->shadow)d->shadow=HeapAlloc(GetProcessHeap(),0,d->bytes);
  if(d->shadow){
   for(ci=first*(d->stride/4);ci<(last+1)*(d->stride/4);ci++)((DWORD*)d->shadow)[ci]=((DWORD*)d->bits)[ci];
   d->shadow_valid=TRUE;
  }
 }else d->shadow_valid=FALSE;
 return n==(int)lines?(int)d->height:0;
}
static void free_record(Dib*d){if(d->shadow)HeapFree(GetProcessHeap(),0,d->shadow);if(d->scratch)HeapFree(GetProcessHeap(),0,d->scratch);if(d->allocation){if(d->section)UnmapViewOfFile(d->allocation);else VirtualFree(d->allocation,0,MEM_RELEASE);}if(d->io)DeleteDC(d->io);if(d->temp)DeleteObject(d->temp);if(d->info)HeapFree(GetProcessHeap(),0,d->info);HeapFree(GetProcessHeap(),0,d);}
HBITMAP WINAPI fp_CreateDIBSection(HDC dc,const BITMAPINFO*info,UINT usage,VOID**bits,HANDLE section,DWORD offset){Dib*d;DWORD colors,infosize,i,h,stride;ULONGLONG length;HBITMAP result;if(bits)*bits=0;if(!info||!bits||info->bmiHeader.biSize<sizeof(BITMAPINFOHEADER)||info->bmiHeader.biWidth<=0||!info->bmiHeader.biHeight||info->bmiHeader.biHeight==(LONG)0x80000000||info->bmiHeader.biPlanes!=1||usage>DIB_PAL_COLORS||(info->bmiHeader.biCompression!=BI_RGB&&info->bmiHeader.biCompression!=BI_BITFIELDS)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}switch(info->bmiHeader.biBitCount){case 1:case 4:case 8:case 16:case 24:case 32:break;default:SetLastError(ERROR_INVALID_PARAMETER);return 0;}h=info->bmiHeader.biHeight<0?-info->bmiHeader.biHeight:info->bmiHeader.biHeight;length=((ULONGLONG)info->bmiHeader.biWidth*info->bmiHeader.biBitCount+31)/32*4;if(length>0x7fffffff){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}stride=(DWORD)length;length*=h;if(length>0x7fffffff){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}colors=info->bmiHeader.biClrUsed;if(!colors&&info->bmiHeader.biBitCount<=8)colors=1u<<info->bmiHeader.biBitCount;if(colors>256||info->bmiHeader.biSize>124){SetLastError(ERROR_INVALID_PARAMETER);return 0;}infosize=info->bmiHeader.biSize+colors*(usage==DIB_PAL_COLORS?2:4);if(info->bmiHeader.biSize==40&&info->bmiHeader.biCompression==BI_BITFIELDS)infosize+=12;d=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*d));if(!d)return 0;d->bytes=(DWORD)length;d->stride=stride;d->height=h;d->usage=usage;d->offset=offset;d->section=section;d->info=HeapAlloc(GetProcessHeap(),0,infosize);if(!d->info){free_record(d);return 0;}for(i=0;i<infosize;i++)((BYTE*)d->info)[i]=((const BYTE*)info)[i];if(section){DWORD base=offset&~65535u,delta=offset-base;if(offset&3){free_record(d);SetLastError(ERROR_INVALID_PARAMETER);return 0;}d->allocation=MapViewOfFile(section,FILE_MAP_READ|FILE_MAP_WRITE,0,base,d->bytes+delta);d->bits=d->allocation?(BYTE*)d->allocation+delta:0;}else{d->allocation=VirtualAlloc(0,d->bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);d->bits=d->allocation;}if(!d->bits){free_record(d);return 0;}{HDC screen=GetDC(0);d->bitmap=screen?CreateCompatibleBitmap(screen,info->bmiHeader.biWidth,h):0;if(screen)ReleaseDC(0,screen);}d->temp=CreateBitmap(1,1,1,1,0);d->io=CreateCompatibleDC(dc);if(!d->bitmap||!d->temp||!d->io){if(d->bitmap)DeleteObject(d->bitmap);free_record(d);return 0;}EnterCriticalSection(&guard);d->next=dibs;dibs=d;result=d->bitmap;*bits=d->bits;LeaveCriticalSection(&guard);return result;}
HGDIOBJ WINAPI fp_SelectObject(HDC dc,HGDIOBJ obj){HGDIOBJ old;Dib*a,*b;EnterCriticalSection(&guard);a=current(dc);old=SelectObject(dc,obj);if(old&&old!=HGDI_ERROR){b=current(dc);if(a!=b){if(a)a->owner=0;if(b)b->owner=dc;}}LeaveCriticalSection(&guard);return old;}
static int rgb_surface(Dib*d){return d&&d->info->bmiHeader.biCompression==BI_RGB&&(d->info->bmiHeader.biBitCount==24||d->info->bmiHeader.biBitCount==32);}
static DWORD pixel_read(Dib*d,int x,int y){BYTE*p=(BYTE*)d->bits+(d->info->bmiHeader.biHeight<0?y:d->height-1-y)*d->stride+x*(d->info->bmiHeader.biBitCount/8);DWORD v=p[0]|((DWORD)p[1]<<8)|((DWORD)p[2]<<16);if(d->info->bmiHeader.biBitCount==32)v|=(DWORD)p[3]<<24;return v;}
static void pixel_write(Dib*d,int x,int y,DWORD v){BYTE*p=(BYTE*)d->bits+(d->info->bmiHeader.biHeight<0?y:d->height-1-y)*d->stride+x*(d->info->bmiHeader.biBitCount/8);p[0]=v;p[1]=v>>8;p[2]=v>>16;if(d->info->bmiHeader.biBitCount==32)p[3]=v>>24;}
/* Exact CPU SRCCOPY for RGB DIBs with unscaled MM_TEXT coordinate mapping.
   Keeps full color and alpha even when the installed display is VGA16. */
static DWORD ratio(DWORD a,DWORD b,DWORD c){DWORD q,r;__asm__ volatile("mull %3; divl %4":"=&a"(q),"=&d"(r):"0"(a),"r"(b),"r"(c):"cc");return q;}
#ifdef FP_DIRECT_PRESENT
/* Experimental display-only SRCCOPY path. Keep DDB synchronization state
   untouched: later GDI reads of the source bitmap still use transfer(). */
static int direct_present(HDC dst,int x,int y,int w,int h,Dib*a,int sx,int sy,DWORD rop){BITMAPINFO bi;DWORD row,k;int r;void*data;POINT p[2];
 if(!rgb_surface(a)||a->info->bmiHeader.biSize!=40||a->info->bmiHeader.biClrUsed||rop!=SRCCOPY||w<=0||h<=0||sx<0||sy<0||sx+w>a->info->bmiHeader.biWidth||sy+h>(int)a->height||GetObjectType(dst)!=OBJ_DC||GetDeviceCaps(dst,TECHNOLOGY)!=DT_RASDISPLAY||GetMapMode(dst)!=MM_TEXT)return 0;
 p[0].x=x;p[0].y=y;p[1].x=x+w;p[1].y=y+h;if(!LPtoDP(dst,p,2)||p[1].x-p[0].x!=w||p[1].y-p[0].y!=h)return 0;
 bi.bmiHeader=a->info->bmiHeader;bi.bmiHeader.biHeight=a->height;data=a->bits;
 if(a->info->bmiHeader.biHeight<0){if(!a->scratch)a->scratch=HeapAlloc(GetProcessHeap(),0,a->bytes);if(!a->scratch)return 0;data=a->scratch;for(row=0;row<a->height;row++)for(k=0;k<a->stride/4;k++)((DWORD*)data)[row*(a->stride/4)+k]=((DWORD*)a->bits)[(a->height-1-row)*(a->stride/4)+k];}
 r=StretchDIBits(dst,x,y,w,h,sx,a->height-sy-h,w,h,data,&bi,DIB_RGB_COLORS,SRCCOPY);return r!=0&&r!=GDI_ERROR;
}
#endif
/* These destinations are memory DCs backed by our bitmap. Their visible
   extent is the bitmap; APIRGN (3) combines application and meta clipping in
   device coordinates. Snapshot it once instead of making a GDI server call
   for every pixel. Keep the native fallback if the snapshot is unavailable. */
static RGNDATA* clip_snapshot(HDC dc,int*mode){HRGN r;int status;DWORD size;RGNDATA*data=0;
 *mode=-1;r=CreateRectRgn(0,0,0,0);if(!r)return 0;
 status=GetRandomRgn(dc,r,3);
 if(status==0)*mode=0;
 else if(status==1){size=GetRegionData(r,0,0);if(size>=sizeof(RGNDATAHEADER)){
  data=HeapAlloc(GetProcessHeap(),0,size);
  if(data){if(GetRegionData(r,size,data)==size&&data->rdh.dwSize==sizeof(RGNDATAHEADER)&&data->rdh.iType==RDH_RECTANGLES&&data->rdh.nCount<=(size-sizeof(RGNDATAHEADER))/sizeof(RECT))*mode=1;
   else{HeapFree(GetProcessHeap(),0,data);data=0;}}
 }}DeleteObject(r);return data;
}
static BOOL clip_contains(HDC dc,int mode,const RGNDATA*data,int x,int y,int lx,int ly){DWORD i;const RECT*r;
 if(mode<0)return PtVisible(dc,lx,ly);if(mode==0)return TRUE;
 r=(const RECT*)data->Buffer;for(i=0;i<data->rdh.nCount;i++)if(x>=r[i].left&&x<r[i].right&&y>=r[i].top&&y<r[i].bottom)return TRUE;return FALSE;
}
static int cpu_copy(HDC dst,Dib*b,int x,int y,int w,int h,HDC src,Dib*a,int sx,int sy,int sw,int sh,DWORD rop){DWORD*temp;RGNDATA*region=0;int clip_mode=-1;POINT dp[2],sp[2];RECT clip;int xx,yy,sourcex,sourcey,clip_type,logical_x=x,logical_y=y;SIZE_T count;if(!rgb_surface(a)||!rgb_surface(b)||rop!=SRCCOPY||w<=0||h<=0||sw<=0||sh<=0)return 0;dp[0].x=x;dp[0].y=y;dp[1].x=x+w;dp[1].y=y+h;sp[0].x=sx;sp[0].y=sy;sp[1].x=sx+sw;sp[1].y=sy+sh;if(!LPtoDP(dst,dp,2)||!LPtoDP(src,sp,2)||dp[1].x-dp[0].x!=w||dp[1].y-dp[0].y!=h||sp[1].x-sp[0].x!=sw||sp[1].y-sp[0].y!=sh)return 0;x=dp[0].x;y=dp[0].y;sx=sp[0].x;sy=sp[0].y;if(sx<0||sy<0||sx+sw>a->info->bmiHeader.biWidth||sy+sh>(int)a->height)return 0;if((ULONGLONG)sw*sh>0x1fffffff)return 0;count=(SIZE_T)sw*sh;temp=HeapAlloc(GetProcessHeap(),0,count*4);if(!temp)return 0;for(yy=0;yy<sh;yy++)for(xx=0;xx<sw;xx++)temp[yy*sw+xx]=pixel_read(a,sx+xx,sy+yy);clip_type=GetClipBox(dst,&clip);if(clip_type==ERROR){HeapFree(GetProcessHeap(),0,temp);return 0;}dp[0].x=clip.left;dp[0].y=clip.top;dp[1].x=clip.right;dp[1].y=clip.bottom;LPtoDP(dst,dp,2);clip.left=dp[0].x;clip.top=dp[0].y;clip.right=dp[1].x;clip.bottom=dp[1].y;if(clip_type==COMPLEXREGION)region=clip_snapshot(dst,&clip_mode);for(yy=0;yy<h;yy++)if(y+yy>=0&&y+yy<(int)b->height&&y+yy>=clip.top&&y+yy<clip.bottom){sourcey=(int)ratio(yy,sh,h);for(xx=0;xx<w;xx++)if(x+xx>=0&&x+xx<b->info->bmiHeader.biWidth&&x+xx>=clip.left&&x+xx<clip.right&&(clip_type!=COMPLEXREGION||clip_contains(dst,clip_mode,region,x+xx,y+yy,logical_x+xx,logical_y+yy))){sourcex=(int)ratio(xx,sw,w);pixel_write(b,x+xx,y+yy,temp[sourcey*sw+sourcex]);}}if(region)HeapFree(GetProcessHeap(),0,region);HeapFree(GetProcessHeap(),0,temp);return 1;}
BOOL WINAPI fp_BitBlt(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,DWORD rop){Dib*a,*b;BOOL ok;static int seen;if(seen++<20)FpObserve("BitBlt rop",rop);EnterCriticalSection(&guard);a=current(src);b=current(dst);
#ifdef FP_DIRECT_PRESENT
if(a&&!b&&direct_present(dst,x,y,w,h,a,sx,sy,rop)){LeaveCriticalSection(&guard);return TRUE;}
#endif
if(cpu_copy(dst,b,x,y,w,h,src,a,sx,sy,w,h,rop)){LeaveCriticalSection(&guard);return TRUE;}if(a)transfer(a,1);if(b&&b!=a)transfer(b,1);ok=BitBlt(dst,x,y,w,h,src,sx,sy,rop);if(ok&&b)transfer(b,0);LeaveCriticalSection(&guard);return ok;}
BOOL WINAPI fp_StretchBlt(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,int sw,int sh,DWORD rop){Dib*a,*b;BOOL ok;EnterCriticalSection(&guard);a=current(src);b=current(dst);if(GetStretchBltMode(dst)!=HALFTONE&&cpu_copy(dst,b,x,y,w,h,src,a,sx,sy,sw,sh,rop)){LeaveCriticalSection(&guard);return TRUE;}if(a)transfer(a,1);if(b&&b!=a)transfer(b,1);ok=StretchBlt(dst,x,y,w,h,src,sx,sy,sw,sh,rop);if(ok&&b)transfer(b,0);LeaveCriticalSection(&guard);return ok;}
BOOL WINAPI fp_DeleteObject(HGDIOBJ obj){Dib*d,**link;BOOL ok;EnterCriticalSection(&guard);d=find(obj);ok=DeleteObject(obj);if(ok&&d){for(link=&dibs;*link;link=&(*link)->next)if(*link==d){*link=d->next;break;}free_record(d);}LeaveCriticalSection(&guard);return ok;}
BOOL WINAPI fp_DeleteDC(HDC dc){Dib*d;BOOL ok;EnterCriticalSection(&guard);d=current(dc);ok=DeleteDC(dc);if(ok&&d)d->owner=0;LeaveCriticalSection(&guard);return ok;}
int WINAPI fp_GetObjectW(HANDLE obj,int size,LPVOID out){Dib*d;DIBSECTION ds;int i,n;EnterCriticalSection(&guard);d=find(obj);if(!d){LeaveCriticalSection(&guard);return GetObjectW(obj,size,out);}if(!out){LeaveCriticalSection(&guard);return sizeof(BITMAP);}for(i=0;i<sizeof(ds);i++)((BYTE*)&ds)[i]=0;ds.dsBm.bmWidth=d->info->bmiHeader.biWidth;ds.dsBm.bmHeight=d->height;ds.dsBm.bmWidthBytes=d->stride;ds.dsBm.bmPlanes=1;ds.dsBm.bmBitsPixel=d->info->bmiHeader.biBitCount;ds.dsBm.bmBits=d->bits;ds.dsBmih=d->info->bmiHeader;ds.dshSection=d->section;ds.dsOffset=d->offset;if(d->info->bmiHeader.biCompression==BI_BITFIELDS){DWORD*mask=(DWORD*)((BYTE*)d->info+40);for(i=0;i<3;i++)ds.dsBitfields[i]=mask[i];}n=size>=sizeof(ds)?sizeof(ds):sizeof(BITMAP);if(size<n){LeaveCriticalSection(&guard);SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}for(i=0;i<n;i++)((BYTE*)out)[i]=((BYTE*)&ds)[i];LeaveCriticalSection(&guard);return n;}
int WINAPI fp_GetDIBits(HDC dc,HBITMAP bitmap,UINT start,UINT lines,LPVOID bits,LPBITMAPINFO info,UINT usage){Dib*d;HGDIOBJ old=0;int n;EnterCriticalSection(&guard);d=find(bitmap);if(d){transfer(d,1);if(d->owner&&GetCurrentObject(d->owner,OBJ_BITMAP)==d->bitmap)old=SelectObject(d->owner,d->temp);}n=GetDIBits(dc,bitmap,start,lines,bits,info,usage);if(old&&old!=HGDI_ERROR)SelectObject(d->owner,old);LeaveCriticalSection(&guard);return n;}
/* GdiFlush is a completion barrier for native GDI batches, not a request
   to upload every CPU buffer. Read/write operations synchronize their DIBs
   individually. Uploading here repeatedly copies surfaces that Flash is
   about to render and dominates early-NT GDI server time. */
BOOL WINAPI fp_GdiFlush(void){return GdiFlush();}
BOOL WINAPI fp_RestoreDC(HDC dc,int saved){Dib*a,*b;BOOL ok;EnterCriticalSection(&guard);a=current(dc);ok=RestoreDC(dc,saved);if(ok){b=current(dc);if(a!=b){if(a)a->owner=0;if(b)b->owner=dc;}}LeaveCriticalSection(&guard);return ok;}
BOOL WINAPI fp_ExtTextOutW(HDC dc,int x,int y,UINT opt,const RECT*r,LPCWSTR s,UINT count,const INT*dx){Dib*d;BOOL ok;EnterCriticalSection(&guard);d=current(dc);if(d)transfer(d,1);ok=ExtTextOutW(dc,x,y,opt,r,s,count,dx);if(ok&&d)transfer(d,0);LeaveCriticalSection(&guard);return ok;}
BOOL WINAPI fp_ExtTextOutA(HDC dc,int x,int y,UINT opt,const RECT*r,LPCSTR s,UINT count,const INT*dx){Dib*d;BOOL ok;EnterCriticalSection(&guard);d=current(dc);if(d)transfer(d,1);ok=ExtTextOutA(dc,x,y,opt,r,s,count,dx);if(ok&&d)transfer(d,0);LeaveCriticalSection(&guard);return ok;}
COLORREF WINAPI fp_SetPixel(HDC dc,int x,int y,COLORREF color){Dib*d;COLORREF result;EnterCriticalSection(&guard);d=current(dc);if(rgb_surface(d)){POINT pt;pt.x=x;pt.y=y;LPtoDP(dc,&pt,1);if(pt.x>=0&&pt.x<d->info->bmiHeader.biWidth&&pt.y>=0&&pt.y<(int)d->height&&PtVisible(dc,x,y)){pixel_write(d,pt.x,pt.y,((color&255)<<16)|(color&0xff00)|((color>>16)&255));LeaveCriticalSection(&guard);return color&0xffffff;}LeaveCriticalSection(&guard);return CLR_INVALID;}if(d)transfer(d,1);result=SetPixel(dc,x,y,color);if(result!=CLR_INVALID&&d)transfer(d,0);LeaveCriticalSection(&guard);return result;}
#define DRAW0(name) BOOL WINAPI fp_##name(HDC dc){Dib*d;BOOL ok;EnterCriticalSection(&guard);d=current(dc);if(d)transfer(d,1);ok=name(dc);if(ok&&d)transfer(d,0);LeaveCriticalSection(&guard);return ok;}
DRAW0(FillPath)
DRAW0(StrokePath)
BOOL WINAPI fp_LineTo(HDC dc,int x,int y){Dib*d;BOOL ok;EnterCriticalSection(&guard);d=current(dc);if(d)transfer(d,1);ok=LineTo(dc,x,y);if(ok&&d)transfer(d,0);LeaveCriticalSection(&guard);return ok;}
BOOL WINAPI fp_PolyBezierTo(HDC dc,const POINT*p,DWORD count){Dib*d;BOOL ok;EnterCriticalSection(&guard);d=current(dc);if(d)transfer(d,1);ok=PolyBezierTo(dc,p,count);if(ok&&d)transfer(d,0);LeaveCriticalSection(&guard);return ok;}

int WINAPI fp_StretchDIBits(HDC dc,int x,int y,int w,int h,int sx,int sy,int sw,int sh,const VOID*bits,const BITMAPINFO*info,UINT usage,DWORD rop){int r;Dib*d;static int seen;if(seen++<20){FpObserve("StretchDIBits pixel",bits?*(const DWORD*)bits:0);FpObserve("StretchDIBits bpp",info?info->bmiHeader.biBitCount:0);}EnterCriticalSection(&guard);d=current(dc);if(d)transfer(d,1);r=StretchDIBits(dc,x,y,w,h,sx,sy,sw,sh,bits,info,usage,rop);if(r&&r!=GDI_ERROR&&d)transfer(d,0);LeaveCriticalSection(&guard);if(seen<21)FpObserve("StretchDIBits result",r);return r;}
/* Expose the software RGB render target to Flash. The physical display remains
   VGA16; final presentation converts RGB to the native display palette. */
int WINAPI fp_GetDeviceCaps(HDC dc,int index){
#ifdef FP_SOFTWARE_RGB
 if(GetDeviceCaps(dc,TECHNOLOGY)==DT_RASDISPLAY){if(index==BITSPIXEL)return 32;if(index==PLANES)return 1;if(index==NUMCOLORS)return -1;if(index==SIZEPALETTE)return 0;if(index==RASTERCAPS)return GetDeviceCaps(dc,index)&~RC_PALETTE;}
#endif
 return GetDeviceCaps(dc,index);
}
