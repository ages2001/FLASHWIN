#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
/* urlmon!CopyStgMedium for early NT. Duplicates a storage medium the way the
   documented API does: shared release via pUnkForRelease (AddRef'd), interface
   mediums AddRef'd, HGLOBAL deep-copied when nobody else owns it. No CRT. */

HRESULT WINAPI fp_CopyStgMedium(const STGMEDIUM*src,STGMEDIUM*dst){
    if(!src||!dst)return E_POINTER;
    *dst=*src;
    switch(src->tymed){
        case TYMED_ISTREAM:
            if(src->pstm) src->pstm->lpVtbl->AddRef(src->pstm);
            break;
        case TYMED_ISTORAGE:
            if(src->pstg) src->pstg->lpVtbl->AddRef(src->pstg);
            break;
        case TYMED_HGLOBAL:
            if(!src->pUnkForRelease && src->hGlobal){
                SIZE_T n=GlobalSize(src->hGlobal);
                HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,n);
                if(!h)return E_OUTOFMEMORY;
                { void*s=GlobalLock(src->hGlobal),*d=GlobalLock(h); SIZE_T i;
                  if(s&&d) for(i=0;i<n;i++)((BYTE*)d)[i]=((BYTE*)s)[i];
                  if(src->hGlobal)GlobalUnlock(src->hGlobal); if(h)GlobalUnlock(h); }
                dst->hGlobal=h;
            }
            break;
        /* TYMED_FILE deep-copy would need CoTaskMemAlloc, which NT 3.1 ole32
           does not export; leave the filename pointer shared (callers that copy
           FILE mediums without a release owner are not exercised here). */
        default: break; /* TYMED_GDI / MFPICT / NULL: pointer copied as-is */
    }
    if(src->pUnkForRelease) src->pUnkForRelease->lpVtbl->AddRef(src->pUnkForRelease);
    return S_OK;
}
