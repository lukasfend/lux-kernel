; =============================================
; Date: 2025-12-11 00:00 UTC
; Author: Lukas Fend <lukas.fend@outlook.com>
; Description: x86 IDT setup and interrupt handler stubs for PS/2 keyboard and exceptions.
; =============================================

[BITS 32]

; IDT gate types
IDT_GATE_TASK       equ 0x5
IDT_GATE_INTERRUPT  equ 0xE
IDT_GATE_TRAP       equ 0xF

section .data
    align 8
    idt_descriptors:
        ; We'll populate this with 48 entries (0x00-0x2F)
        ; Each descriptor is 8 bytes
        times 48 dq 0
    
    idt_register:
        dw (48 * 8) - 1     ; limit (size - 1)
        dd idt_descriptors  ; base address

section .text

; Helper macro to create IDT gate descriptor
; args: vector_number, handler_address, gate_type (IDT_GATE_*)
%macro create_idt_entry 3
    mov eax, %2                    ; handler address
    mov edx, idt_descriptors + (%1 * 8)
    
    mov ecx, eax
    and ecx, 0xFFFF                ; low 16 bits of handler
    mov [edx], cx                  ; offset_low
    
    mov word [edx + 2], 0x08       ; code segment selector (from GDT)
    
    mov byte [edx + 4], 0x00       ; reserved
    mov byte [edx + 5], 0x80 | %3  ; P=1, DPL=0, gate_type
    
    shr eax, 16                    ; high 16 bits of handler
    mov [edx + 6], ax              ; offset_high
%endmacro

; Exception handler stubs - all halt the CPU
; extern exception_handler_stub

; IRQ handler stubs
; extern irq_keyboard_handler

; PIC I/O ports
PIC1_CMD    equ 0x20
PIC1_DATA   equ 0x21
PIC2_CMD    equ 0xA0
PIC2_DATA   equ 0xA1

; PIC control words
ICW1        equ 0x11    ; ICW1: initialize
ICW4        equ 0x01    ; ICW4: 8086 mode
PIC1_OFFSET equ 0x20    ; vectors 0x20-0x27 for master
PIC2_OFFSET equ 0x28    ; vectors 0x28-0x2F for slave
EOI         equ 0x20    ; End of Interrupt

global idt_init
global interrupt_enable
global interrupt_disable

idt_init:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    
    ; Set up exception handlers (vectors 0x00-0x1F)
    xor ecx, ecx
.exception_loop:
    cmp ecx, 32
    jge .exception_done
    
    lea edx, [ecx * 8]
    add edx, idt_descriptors
    
    mov eax, exception_handler_stub
    
    ; Set low 16 bits of handler address
    mov ebx, eax
    and ebx, 0xFFFF
    mov [edx], bx
    
    ; Set code segment and gate type (trap gate, present, ring 0)
    mov word [edx + 2], 0x08
    mov byte [edx + 4], 0x00
    mov byte [edx + 5], 0x8F    ; P=1, DPL=0, gate_type=trap
    
    ; Set high 16 bits of handler address
    mov ebx, eax
    shr ebx, 16
    mov [edx + 6], bx
    
    inc ecx
    jmp .exception_loop

.exception_done:
    ; Set up IRQ0 handler (vector 0x20 = IRQ0, timer)
    mov eax, irq_timer_handler
    mov edx, idt_descriptors + (0x20 * 8)
    
    ; Set low 16 bits
    mov ebx, eax
    and ebx, 0xFFFF
    mov [edx], bx
    
    ; Set code segment and gate type (interrupt gate, present, ring 0)
    mov word [edx + 2], 0x08
    mov byte [edx + 4], 0x00
    mov byte [edx + 5], 0x8E    ; P=1, DPL=0, gate_type=interrupt
    
    ; Set high 16 bits
    mov ebx, eax
    shr ebx, 16
    mov [edx + 6], bx
    
    ; Set up IRQ handler (vector 0x21 = IRQ1, keyboard)
    mov eax, irq_keyboard_handler
    mov edx, idt_descriptors + (0x21 * 8)
    
    ; Set low 16 bits
    mov ebx, eax
    and ebx, 0xFFFF
    mov [edx], bx
    
    ; Set code segment and gate type (interrupt gate, present, ring 0)
    mov word [edx + 2], 0x08
    mov byte [edx + 4], 0x00
    mov byte [edx + 5], 0x8E    ; P=1, DPL=0, gate_type=interrupt
    
    ; Set high 16 bits
    mov ebx, eax
    shr ebx, 16
    mov [edx + 6], bx
    
    ; Remap PIC
    ; ICW1 to both PICs
    mov al, ICW1
    out PIC1_CMD, al
    out PIC2_CMD, al
    
    ; ICW2: Set vector offsets
    mov al, PIC1_OFFSET
    out PIC1_DATA, al
    mov al, PIC2_OFFSET
    out PIC2_DATA, al
    
    ; ICW3: Set up cascading (master and slave relationship)
    mov al, 0x04        ; slave on IRQ2
    out PIC1_DATA, al
    mov al, 0x02        ; slave ID is 2
    out PIC2_DATA, al
    
    ; ICW4: 8086 mode
    mov al, ICW4
    out PIC1_DATA, al
    out PIC2_DATA, al
    
    ; Mask all interrupts except IRQ0 (timer) and IRQ1 (keyboard)
    mov al, 0xFC        ; 11111100 = enable IRQ0 and IRQ1 only
    out PIC1_DATA, al
    mov al, 0xFF        ; mask all slave interrupts for now
    out PIC2_DATA, al
    
    ; Load the IDT
    lidt [idt_register]
    
    pop edx
    pop ecx
    pop ebx
    pop ebp
    ret

interrupt_enable:
    sti
    ret

interrupt_disable:
    cli
    ret

; Exception handler stub - all exceptions halt the CPU
global exception_handler_stub
exception_handler_stub:
    cli
    hlt
    jmp exception_handler_stub

; IRQ0 timer handler - acknowledge and call C handler
global irq_timer_handler
irq_timer_handler:
    ; Push registers to preserve them
    push eax
    push ecx
    push edx
    push esi
    push edi
    
    ; Call the C timer handler
    call timer_irq_handler_c
    
    ; Send EOI (End of Interrupt) to master PIC
    mov al, EOI
    out PIC1_CMD, al
    
    ; Pop registers
    pop edi
    pop esi
    pop edx
    pop ecx
    pop eax
    
    iret

; IRQ keyboard handler - acknowledge and call C handler
global irq_keyboard_handler
irq_keyboard_handler:
    ; Push registers to preserve them
    push eax
    push ecx
    push edx
    push esi
    push edi
    
    ; Call the C keyboard handler
    call keyboard_irq_handler_c
    
    ; Send EOI (End of Interrupt) to master PIC
    mov al, EOI
    out PIC1_CMD, al
    
    ; Pop registers
    pop edi
    pop esi
    pop edx
    pop ecx
    pop eax
    
    iret

; C function that the interrupt handler will call
extern keyboard_irq_handler_c
extern timer_irq_handler_c
