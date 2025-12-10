ORG 0x00
BITS 16

_start:
    jmp short start
    nop
; See https://wiki.osdev.org/FAT#BPB_(BIOS_Parameter_Block) -> create 33 bytes for placeholders as fat arguments
times 33 db 0

start:
    jmp 0x7c0:step2

step2:
    cli ; Clear interrupts
    mov ax, 0x7c0
    mov ds, ax
    mov es, ax
    mov ax, 0x00
    mov ss, ax
    mov sp, 0x7C00
    sti ; Enable interrupts

    mov si, message
    call print
    jmp $ ; HALT


print:
    mov bx, 0
.loop:
    lodsb
    cmp al, 0
    je .done
    call print_char
    jmp .loop
.done:
    ret

print_char:
    mov ah, 0Eh
    int 0x10 ; Interrupts: https://www.ctyme.com/intr/int.htm
    ret

message: db 'debug test 123', 0

times 510 - ($ - $$) db 0
dw 0xAA55