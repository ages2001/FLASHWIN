#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <objidl.h>
/* ole32 gap closures. CoInitializeEx maps to CoInitialize; PropVariantClear and
   ReleaseStgMedium are implemented directly; the OLE clipboard / bind-context
   calls are forwarded to ole32 dynamically when present (OLE2 shipped with
   NT 3.1) and fail cleanly otherwise. No CRT. */

static FARPROC ole_proc(const char*name){
    HMODULE h=GetModuleHandleA("ole32.dll"); if(!h)h=LoadLibraryA("ole32.dll");
    return h?GetProcAddress(h,name):0;
}
/* The proxy supplies its own CoInitialize/CoTaskMemFree (NT 3.1 ole32 does not
   export them); call those, not the system imports. */
extern HRESULT WINAPI fp_CoInitialize(LPVOID);
extern void    WINAPI fp_CoTaskMemFree(LPVOID);
static void sysfree(void*b){
    void (WINAPI *f)(void*)=(void(WINAPI*)(void*))(void*)0;
    HMODULE h=GetModuleHandleA("oleaut32.dll"); if(!h)h=LoadLibraryA("oleaut32.dll");
    if(h) f=(void(WINAPI*)(void*))GetProcAddress(h,"SysFreeString");
    if(f&&b) f(b);
}

HRESULT WINAPI fp_CoInitializeEx(LPVOID reserved,DWORD coInit){
    (void)coInit; return fp_CoInitialize(reserved);
}
/* Single-apartment legacy: security blanket is a no-op. */
HRESULT WINAPI fp_CoSetProxyBlanket(IUnknown*p,DWORD a,DWORD b,OLECHAR*c,DWORD d,DWORD e,void*f,DWORD g){
    (void)p;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; return S_OK;
}
void WINAPI fp_PropVariantClear(PROPVARIANT*pv){
    if(!pv) return;
    switch(pv->vt){
        case VT_BSTR: if(pv->bstrVal) sysfree(pv->bstrVal); break;
        case VT_LPSTR: if(pv->pszVal) fp_CoTaskMemFree(pv->pszVal); break;
        case VT_LPWSTR: if(pv->pwszVal) fp_CoTaskMemFree(pv->pwszVal); break;
        case VT_UNKNOWN: case VT_DISPATCH:
            if(pv->punkVal) pv->punkVal->lpVtbl->Release(pv->punkVal); break;
        default: break;
    }
    pv->vt=VT_EMPTY;
}
void WINAPI fp_ReleaseStgMedium(STGMEDIUM*m){
    if(!m) return;
    switch(m->tymed){
        case TYMED_HGLOBAL: if(!m->pUnkForRelease && m->hGlobal) GlobalFree(m->hGlobal); break;
        case TYMED_ISTREAM: if(m->pstm) m->pstm->lpVtbl->Release(m->pstm); break;
        case TYMED_ISTORAGE:if(m->pstg) m->pstg->lpVtbl->Release(m->pstg); break;
        case TYMED_FILE:    if(!m->pUnkForRelease && m->lpszFileName) fp_CoTaskMemFree(m->lpszFileName); break;
        case TYMED_GDI:     if(!m->pUnkForRelease && m->hGlobal) DeleteObject(m->hGlobal); break;
        default: break;
    }
    if(m->pUnkForRelease) m->pUnkForRelease->lpVtbl->Release(m->pUnkForRelease);
    m->tymed=TYMED_NULL; m->pUnkForRelease=0;
}
/* Bind context / display-name / clipboard: forward to ole32 if available. */
HRESULT WINAPI fp_CreateBindCtx(DWORD r,LPBC*ppbc){
    HRESULT (WINAPI *f)(DWORD,LPBC*)=(HRESULT(WINAPI*)(DWORD,LPBC*))ole_proc("CreateBindCtx");
    if(ppbc)*ppbc=0; if(!f) return E_NOTIMPL; return f(r,ppbc);
}
HRESULT WINAPI fp_MkParseDisplayName(LPBC bc,LPCOLESTR sz,ULONG*eaten,LPMONIKER*mk){
    HRESULT (WINAPI *f)(LPBC,LPCOLESTR,ULONG*,LPMONIKER*)=(HRESULT(WINAPI*)(LPBC,LPCOLESTR,ULONG*,LPMONIKER*))ole_proc("MkParseDisplayName");
    if(mk)*mk=0; if(eaten)*eaten=0; if(!f) return E_NOTIMPL; return f(bc,sz,eaten,mk);
}
HRESULT WINAPI fp_OleSetClipboard(IDataObject*d){
    HRESULT (WINAPI *f)(IDataObject*)=(HRESULT(WINAPI*)(IDataObject*))ole_proc("OleSetClipboard");
    if(!f) return S_OK; return f(d);
}
HRESULT WINAPI fp_OleGetClipboard(IDataObject**d){
    HRESULT (WINAPI *f)(IDataObject**)=(HRESULT(WINAPI*)(IDataObject**))ole_proc("OleGetClipboard");
    if(d)*d=0; if(!f) return E_FAIL; return f(d);
}
HRESULT WINAPI fp_OleFlushClipboard(void){
    HRESULT (WINAPI *f)(void)=(HRESULT(WINAPI*)(void))ole_proc("OleFlushClipboard");
    if(!f) return S_OK; return f();
}
HRESULT WINAPI fp_OleIsCurrentClipboard(IDataObject*d){
    HRESULT (WINAPI *f)(IDataObject*)=(HRESULT(WINAPI*)(IDataObject*))ole_proc("OleIsCurrentClipboard");
    if(!f) return S_FALSE; return f(d);
}
