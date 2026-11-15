; TayebOS - cooperative task switch (Phase 4)
[BITS 32]

global task_switch

; void task_switch(uint32_t *old_esp_save, uint32_t new_esp)
; Saves callee-saved registers of the current task, swaps stacks,
; restores the next task's registers and returns into it.
task_switch:
    mov eax, [esp + 4]        ; &old_esp_save
    mov edx, [esp + 8]        ; new_esp
    push esi
    push edi
    push ebx
    push ebp
    mov [eax], esp
    mov esp, edx
    pop ebp
    pop ebx
    pop edi
    pop esi
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
