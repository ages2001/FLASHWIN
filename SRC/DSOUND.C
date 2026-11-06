#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* dsound gap closures (by ordinal). #1 = DirectSoundCreate, #8 =
   DirectSoundCaptureEnumerateW. DirectSound is not provided on early NT (the
   player already falls back to the waveOut mixer), so playback creation reports
   "no driver" and capture enumeration reports zero devices. Clean, non-aborting,
   correct stdcall arg sizes. NON-FUNCTIONAL (no DirectSound). No CRT. */

#define FP_DSERR_NODRIVER 0x88780078L

/* ordinal 1: DirectSoundCreate(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN) -> @12 */
HRESULT WINAPI fp_dsOrdinal1(void*guid,void**ppDS,void*unk){
    (void)guid;(void)unk; if(ppDS)*ppDS=0; return FP_DSERR_NODRIVER;
}
/* ordinal 8: DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW, LPVOID) -> @8.
   No capture devices: succeed without invoking the callback. */
HRESULT WINAPI fp_dsOrdinal8(void*cb,void*ctx){ (void)cb;(void)ctx; return S_OK; }
