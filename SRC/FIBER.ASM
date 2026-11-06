bits 32
section .text
; void __stdcall fp_ctx_switch(void **save_esp, void *load_esp)
; Saves callee-saved regs + esp into *save_esp, then loads load_esp and
; restores the regs saved there. Cooperative fiber context switch.
global _fp_ctx_switch@8
_fp_ctx_switch@8:
    push ebp
    push ebx
    push esi
    push edi
    mov  eax, [esp+20]     ; save_esp  (4 pushes*4 + ret4 = 20)
    mov  [eax], esp        ; *save_esp = current esp
    mov  eax, [esp+24]     ; load_esp
    mov  esp, eax          ; switch stacks
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret  8

; The initial entry for a freshly created fiber. The fiber's prepared stack
; "returns" here with the Fiber* in edi (restored from the stack image).
extern _fp_fiber_start      ; void fp_fiber_start(void)
global _fp_fiber_entry
_fp_fiber_entry:
    call _fp_fiber_start
    ; fp_fiber_start never returns (it ExitThreads if the fiber proc returns)
    ret
