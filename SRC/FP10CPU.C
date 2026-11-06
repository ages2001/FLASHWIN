#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "cpu_features.h"

/* No SIMD instructions are permitted in this DLL. Zero is the safe mode
   before the executable's entry gate has run (including TLS callbacks). */
static BOOL selected_sse2;
static BOOL initialized;

void WINAPI FpInitializeCpuMode(void) {
    const DWORD required=(1u<<24)|(1u<<25)|(1u<<26);
    if(!initialized) {
        selected_sse2=(fp_cpu_edx()&required)==required && fp_xmm_context_ready();
        initialized=TRUE;
    }
}
BOOL WINAPI FpSse2Enabled(void) { return selected_sse2; }

/* r is the x86 PUSHAD layout: EDI, ESI, EBP, saved ESP, EBX, EDX, ECX, EAX.
   Preserve hardware results except features deliberately outside this port's
   SSE2 ceiling. Never invent a feature that the processor did not report. */
void WINAPI FpFilterCpuid(DWORD leaf,DWORD *r) {
    if(leaf==1) {
        if(!selected_sse2)r[5]&=~(3u<<25);
        r[6]&=~((1u<<0)|(1u<<1)|(1u<<9)|(1u<<12)|(1u<<19)|(1u<<20)|
                 (1u<<25)|(1u<<26)|(1u<<27)|(1u<<28)|(1u<<29));
    } else if(leaf==7) {
        r[7]=r[4]=r[6]=r[5]=0;
    } else if(leaf==0x80000001u) {
        r[6]&=0xfffee7bfu;
    }
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) { return TRUE; }
