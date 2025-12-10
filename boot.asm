ORG 0x7C00
BITS 16

start:
    mov ah, 0Eh
    mov al, 'A'
    mov bx, 0
    int 0x10 ; Interrupts: https://www.ctyme.com/intr/int.htm
    
    jmp $ ; HALT

times 510 - ($ - $$) db 0
dw 0xAA55