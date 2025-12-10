[BITS 32]

CODE_SEG equ 0x08
DATA_SEG equ 0x10

global _start
extern kmain

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

    call kmain

.hang:
    hlt
    jmp .hang