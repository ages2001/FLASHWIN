#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* mscms (ICM colour management) gap closures. Early NT has no ICM; implement an
   identity colour pipeline: profiles/transforms are opaque non-null handles and
   TranslateBitmapBits copies the source through unchanged (uncorrected colour,
   which is exactly what the platform does without ICM). No CRT. */

typedef PVOID HPROFILE;
typedef PVOID HTRANSFORM;

HPROFILE WINAPI fp_OpenColorProfileW(void*profile,DWORD access,DWORD share,DWORD create){
    (void)profile;(void)access;(void)share;(void)create;
    return (HPROFILE)(ULONG_PTR)1;   /* opaque identity profile */
}
BOOL WINAPI fp_CloseColorProfile(HPROFILE h){(void)h;return TRUE;}
HTRANSFORM WINAPI fp_CreateColorTransformW(void*lcs,HPROFILE dst,HPROFILE tgt,DWORD flags){
    (void)lcs;(void)dst;(void)tgt;(void)flags;
    return (HTRANSFORM)(ULONG_PTR)1; /* opaque identity transform */
}
BOOL WINAPI fp_DeleteColorTransform(HTRANSFORM h){(void)h;return TRUE;}
/* Identity transform: copy the bitmap through unchanged. */
BOOL WINAPI fp_TranslateBitmapBits(HTRANSFORM h,PVOID src,DWORD inFmt,DWORD w,DWORD hgt,
                                   DWORD inStride,PVOID dst,DWORD outFmt,DWORD outStride,
                                   void*cb,LPARAM lp){
    DWORD y; (void)h;(void)inFmt;(void)outFmt;(void)cb;(void)lp;
    if(!src||!dst||!w||!hgt) return FALSE;
    if(src==dst) return TRUE;
    { DWORD row = inStride<outStride?inStride:outStride;
      for(y=0;y<hgt;y++){
          BYTE*s=(BYTE*)src+(SIZE_T)y*inStride, *d=(BYTE*)dst+(SIZE_T)y*outStride; DWORD i;
          for(i=0;i<row;i++) d[i]=s[i];
      } }
    return TRUE;
}
