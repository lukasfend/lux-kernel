; =============================================
; Date: 2025-12-10 00:00 UTC
; Author: Lukas Fend <lukas.fend@outlook.com>
; Description: Boot sector that loads the kernel and jumps to protected mode.
; =============================================
ORG 0x7C00
BITS 16

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

KERNEL_LOAD_SEG    equ 0x1000
KERNEL_LOAD_OFFSET equ 0x0000
KERNEL_LOAD_ADDR   equ (KERNEL_LOAD_SEG << 4) + KERNEL_LOAD_OFFSET

%include "kernel_sectors.inc"
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1
%endif

_start:
    jmp short start
    nop
; BIOS Parameter Block placeholder (33 bytes)
times 33 db 0

boot_drive: db 0

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    call load_kernel
    call enable_a20
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_protected_mode

; ------------------------------
; Real-mode helpers
; ------------------------------

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

load_kernel:
    mov cx, KERNEL_SECTORS
    mov dword [kernel_load_ptr], KERNEL_LOAD_ADDR
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0

load_next_sector:
    mov eax, [kernel_load_ptr]
    mov dx, ax
    and dx, 0x000F
    mov [dap_buffer_offset], dx
    shr eax, 4
    mov [dap_buffer_segment], ax
    mov word [dap_sector_count], 1

    mov dl, [boot_drive]
    mov si, disk_address_packet
    mov ah, 0x42
    int 0x13
    jc disk_error

    add dword [kernel_load_ptr], 512
    add dword [dap_lba_low], 1
    adc dword [dap_lba_high], 0
    loop load_next_sector
    ret

disk_error:
    mov si, error_message
print_error:
    lodsb
    or al, al
    jz hang
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_error

hang:
    hlt
    jmp hang

; ------------------------------
; Protected mode entry
; ------------------------------

[BITS 32]
init_protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00200000
    mov ebp, esp
    jmp KERNEL_LOAD_ADDR

halt:
    hlt
    jmp halt

[BITS 16]

; ------------------------------
; GDT
; ------------------------------

gdt_start:
gdt_null:
    dd 0
    dd 0

gdt_code:
    dw 0xFFFF
    dw 0
    db 0
    db 0x9A
    db 11001111b
    db 0

gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 0x92
    db 11001111b
    db 0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Disk Address Packet (for INT 13h extensions)
disk_address_packet:
    db 0x10
    db 0
dap_sector_count:
    dw 0
dap_buffer_offset:
    dw 0
dap_buffer_segment:
    dw 0
dap_lba_low:
    dd 0
dap_lba_high:
    dd 0

kernel_load_ptr: dd 0

error_message: db 'Failed to load kernel via BIOS', 0

times 510 - ($ - $$) db 0
dw 0xAA55