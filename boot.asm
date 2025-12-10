ORG 0x00
BITS 16

_start:
    jmp short start
    nop
; See https://wiki.osdev.org/FAT#BPB_(BIOS_Parameter_Block) -> create 33 bytes for placeholders as fat arguments
times 33 db 0

start:
    jmp 0x7C0:step2




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
    mov ah, 2 ; Read sector
    mov al, 1 ; 1 Sector to read
    mov ch, 0 ; Cylinder number
    mov cl, 2 ; read sector 2
    mov dh, 0 ; Head number
    mov bx, buffer
    int 0x13 ; Invoke read command
    
    jc error ; jump-carry to error lbl

    jmp $ ; HALT

error:
    mov si, error_message
    call print
    jmp $

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

error_message: db 'Failed to load sector', 0

times 510 - ($ - $$) db 0
dw 0xAA55

; Empty buffer to be written to
buffer: