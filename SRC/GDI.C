#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* gdi32 gap closures for early NT. No CRT, no SIMD. */
static void bcopy_(void*d,const void*s,unsigned n){unsigned i;for(i=0;i<n;i++)((BYTE*)d)[i]=((const BYTE*)s)[i];}
static void bzero_(void*d,unsigned n){unsigned i;for(i=0;i<n;i++)((BYTE*)d)[i]=0;}

/* NT 3.1/3.5x have no ICM; report no profile so the caller uses uncorrected colour. */
BOOL WINAPI fp_GetICMProfileA(HDC hdc,LPDWORD pBufSize,LPSTR pszFilename){
    (void)hdc;(void)pszFilename; if(pBufSize)*pBufSize=0;
    SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
}

typedef struct{FONTENUMPROCW proc;LPARAM lp;}FpFontCtx;
static int CALLBACK fp_font_thunk(const LOGFONTW*lf,const TEXTMETRICW*tm,DWORD type,LPARAM lp){
    FpFontCtx*c=(FpFontCtx*)lp; ENUMLOGFONTEXW ex; NEWTEXTMETRICEXW nx;
    bzero_(&ex,sizeof(ex)); bzero_(&nx,sizeof(nx));
    bcopy_(&ex,lf,sizeof(ENUMLOGFONTW));
    bcopy_(&nx.ntmTm,tm,(type&TRUETYPE_FONTTYPE)?sizeof(NEWTEXTMETRICW):sizeof(TEXTMETRICW));
    return c->proc((const LOGFONTW*)&ex,(const TEXTMETRICW*)&nx,type,c->lp);
}
/* Ex form is absent on early NT; thunk through EnumFontFamiliesW and widen records. */
int WINAPI fp_EnumFontFamiliesExW(HDC hdc,LPLOGFONTW lf,FONTENUMPROCW proc,LPARAM lp,DWORD flags){
    FpFontCtx c; LPCWSTR face; (void)flags; c.proc=proc; c.lp=lp;
    face=(lf&&lf->lfFaceName[0])?lf->lfFaceName:0;
    return EnumFontFamiliesW(hdc,face,(FONTENUMPROCW)fp_font_thunk,(LPARAM)&c);
}
