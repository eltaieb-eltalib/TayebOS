; ============================================================
;  TayebOS - ISR / IRQ stubs (Phase 2)
;  - Exceptions 0-31: push dummy error code where the CPU
;    doesn't push one, then the vector number.
;  - IRQs 0-15 (vectors 32-47): same, then the vector number.
;  - Common stubs save registers, call the C handler with
;    (int_no, err_code) / (irq_no), restore and iret.
;  - isr_stubs[] is a table of all 48 handler addresses so
;    the C code can fill the IDT in a loop.
; ============================================================
[BITS 32]
[EXTERN isr_handler]
[EXTERN irq_handler]

global load_idt
global isr_stubs

load_idt:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ---------- common stubs ----------
isr_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; stack now: [ds][edi esi ebp esp ebx edx ecx eax][int_no][err]
    mov eax, [esp+36]          ; int_no
    mov ebx, [esp+40]          ; err_code
    push ebx
    push eax
    call isr_handler
    add esp, 8
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8                 ; discard int_no / err_code
    iret

irq_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, [esp+36]          ; vector (32..47)
    sub eax, 32                ; -> irq number 0..15
    push eax
    call irq_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8                 ; discard int_no / err_code
    iret

; ---------- exception stubs ----------
%macro ISR_NOERR 1
global isr%1
isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    cli
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; ---------- IRQ stubs (vectors 32-47) ----------
%macro IRQ_STUB 2
global irq%1
irq%1:
    cli
    push dword 0
    push dword %2
    jmp irq_common
%endmacro

IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

; ---------- handler table ----------
isr_stubs:
    dd isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    dd isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    dd irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7
    dd irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15

section .note.GNU-stack noalloc noexec nowrite progbits
