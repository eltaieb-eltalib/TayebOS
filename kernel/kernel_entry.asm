; TayebOS - kernel entry stub
; The bootloader jumps here (physical 0x10000) in 32-bit protected mode.
[BITS 32]
[EXTERN kernel_main]

global _start
_start:
    mov esp, 0x90000
    call kernel_main
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
