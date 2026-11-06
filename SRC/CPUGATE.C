#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../compat/cpu_features.h"

/* Both checks run on a 386 baseline, before the original FP32 entry point. */
BOOL WINAPI FpHasSse2(void) {
    return (fp_cpu_edx()&(1u<<26))!=0;
}

void WINAPI FpRequireSse2(void) {
    if(!FpHasSse2()) {
        MessageBoxA(0,
          "Flash Player 32 requires an SSE2-capable processor.\r\n\r\n"
          "SSE2 was not detected. Flash Player 32 will now close.\r\n"
          "Use Flash Player 10 on this processor.",
          "Flash Player 32 - SSE2 required",MB_OK|MB_ICONSTOP);
        ExitProcess(ERROR_NOT_SUPPORTED);
    }
    if(!fp_xmm_context_ready()) {
        MessageBoxA(0,
          "Flash Player 32 requires working SSE2 context support.\r\n\r\n"
          "This system does not preserve SSE (XMM) registers between threads.\r\n"
          "An SSE enabler alone is not enough: install WINSIMD 1.2 or later,\r\n"
          "which also preserves the SSE registers, or use Flash Player 10.\r\n"
          "Flash Player 32 will now close.",
          "Flash Player 32 - SSE2 support required",MB_OK|MB_ICONSTOP);
        ExitProcess(ERROR_NOT_SUPPORTED);
    }
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){return TRUE;}
