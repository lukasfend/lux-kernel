ORG 0x00
BITS 16

_start:
    jmp short start
    nop
; See https://wiki.osdev.org/FAT#BPB_(BIOS_Parameter_Block) -> create 33 bytes for placeholders as fat arguments
times 33 db 0

start:
    jmp 0x7C0:step2

handle_zero:
    mov ah, 0Eh
    mov al, 'A'
    mov bx, 0x00
    int 0x10
    iret

handle_one:
    mov ah, 0Eh
    mov al, 'V'
    mov bx, 0x00
    int 0x10
    iret

step2:
    cli ; Clear interrupts
    mov ax, 0x7C0
    mov ds, ax
    mov es, ax
    mov ax, 0x00
    mov ss, ax
    mov sp, 0x7C00
    sti ; Enable interrupts

    ; Set interrupt vector table; stack segment -> assign interrupt logic
    ; Exceptions: https://wiki.osdev.org/Exceptions 
    ; Interrupt 0
    mov word[ss:0x00], handle_zero
    mov word[ss:0x02], 0x7C0

    ; Interrupt 1
    mov word[ss:0x04], handle_one
    mov word[ss:0x06], 0x7C0

    mov ax, 0x00
    div ax

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