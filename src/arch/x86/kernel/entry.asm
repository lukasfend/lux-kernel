; =============================================
; Date: 2025-12-10 00:00 UTC
; Author: Lukas Fend <lukas.fend@outlook.com>
; Description: Protected-mode entry stub that prepares segments and calls kernel.
; =============================================
[BITS 32]

CODE_SEG equ 0x08
DATA_SEG equ 0x10

global _start
extern kernel

_start:
    cli
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00200000
    mov ebp, esp

    call kernel

.hang:
    hlt
    jmp .hang