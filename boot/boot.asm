; ============================================================
;  TayebOS - Bootloader v1.1 (BIOS boot sector, 512 bytes)
;  Boots from floppy, hard disk, or USB stick (Rufus DD image):
;  the kernel is loaded with EDD LBA reads (AH=42h) when the
;  BIOS offers them - the reliable path on USB/HDD - and falls
;  back to a one-sector-at-a-time CHS loop using the geometry
;  the BIOS reports for the boot drive (DL) on floppies and
;  old BIOSes. (A single big CHS read hangs on SeaBIOS USB.)
;  - Copies the BIOS 8x8 font to physical 0x9000 for the GUI
;  - Sets VGA Mode 13h, enables A20, flat GDT, protected mode
; ============================================================
[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET  equ 0x10000
KERNEL_SECTORS equ 64         ; 64 * 512 = 32 KiB max kernel

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    mov si, msg_loading
    call print16

    ; ---- Drive geometry: int 13h AH=08h -> DH=last head, CL&3Fh=spt ----
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    jc .geom_default
    mov al, cl
    and al, 0x3F
    mov [spt], al
    xor ah, ah
    mov [spt_w], ax
    inc dh
    mov [heads], dh
    mov al, dh
    xor ah, ah
    mov [heads_w], ax
    jmp .geom_done
.geom_default:
    mov byte [spt], 18
    mov byte [heads], 2
.geom_done:

    ; ---- Copy BIOS 8x8 font (128 chars x 8 bytes) to 0x9000 ----
    mov ax, 0x1130
    mov bh, 0x03
    int 0x10
    mov ax, es
    mov ds, ax
    mov si, bp
    mov ax, 0x0900
    mov es, ax
    xor di, di
    mov cx, 1024
    rep movsb
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; ---- Load kernel to physical 0x10000 ----
    ; Preferred: EDD LBA read (AH=42h) - the reliable path for
    ; USB and hard disks. Fallback: CHS loop, one sector at a
    ; time (floppies and old BIOSes without extensions).
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .chs_loop
    cmp bx, 0xAA55
    jne .chs_loop

    mov byte [edd_chunks], 2        ; 2 chunks x 32 sectors = 64
.edd_loop:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    add word [dap_lba], 32
    add word [dap_seg], 0x400       ; +16 KiB (32 sectors)
    dec byte [edd_chunks]
    jnz .edd_loop
    jmp .load_done

.chs_loop:
    mov ax, 0x1000
    mov es, ax
    xor bx, bx                ; ES:BX = destination
    mov word [cur_lba], 1     ; kernel starts at LBA 1
    mov si, KERNEL_SECTORS
.chs_read:
    ; LBA -> CHS: sector = lba % spt + 1, head = (lba/spt) % heads,
    ;             cyl = (lba/spt) / heads
    mov ax, [cur_lba]
    xor dx, dx
    div word [spt_w]          ; AX = lba/spt, DX = lba%spt
    mov cl, dl
    inc cl                    ; CL = sector
    xor dx, dx
    div word [heads_w]        ; AX = cyl, DX = head
    mov dh, dl                ; DH = head
    mov ch, al                ; CH = cylinder (low 8 bits; ours is 0/1)
    mov ah, 0x02
    mov al, 0x01              ; read exactly 1 sector
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    add bx, 512
    inc word [cur_lba]
    dec si
    jnz .chs_read
.load_done:

    ; ---- Set VGA Mode 13h: 320x200, 256 colors, FB at 0xA0000 ----
    mov ax, 0x0013
    int 0x10

    ; ---- Enable A20 (fast, port 0x92) ----
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; ---- Switch to Protected Mode ----
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp CODE_SEG:init_pm

disk_error:
    mov si, msg_error
    call print16
    jmp $

print16:
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

[BITS 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_OFFSET

gdt_start:
    dq 0x0000000000000000
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

boot_drive db 0
spt        db 18
heads      db 2
spt_w      dw 18
heads_w    dw 2
cur_lba    dw 1
edd_chunks db 0
dap:       db 16, 0           ; Disk Address Packet (16 bytes)
dap_count: dw 32              ; sectors per chunk
dap_off:   dw 0
dap_seg:   dw 0x1000
dap_lba:   dd 1
           dd 0
msg_loading db "TayebOS v1.1 bootloader: loading kernel + GUI...", 13, 10, 0
msg_error   db "Disk read error!", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
