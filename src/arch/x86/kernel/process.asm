; =============================================
; Date: 2025-12-11 00:00 UTC
; Author: Lukas Fend <lukas.fend@outlook.com>
; Description: Context switching and task switching for process management.
; =============================================

[BITS 32]

section .text

; CPU context structure offsets (must match struct cpu_context in process.h)
%define CONTEXT_EAX    0
%define CONTEXT_EBX    4
%define CONTEXT_ECX    8
%define CONTEXT_EDX   12
%define CONTEXT_ESI   16
%define CONTEXT_EDI   20
%define CONTEXT_EBP   24
%define CONTEXT_ESP   28
%define CONTEXT_EIP   32
%define CONTEXT_EFLAGS 36

; Save the current CPU context to the provided structure.
;
; void process_save_context(struct cpu_context *context);
;
; Called with EAX = pointer to struct cpu_context
; Saves all general purpose registers and the current return address as EIP.
global process_save_context
process_save_context:
    mov edx, [esp + 4]           ; edx = context pointer (from stack)
    
    ; Get the return address (what called this function)
    mov eax, [esp]
    mov [edx + CONTEXT_EIP], eax  ; Save return address as EIP
    
    ; Save ESP (stack pointer of caller, before call)
    mov eax, [esp + 4]
    add eax, 4                     ; ESP was pointing to saved EIP, so add 4
    mov [edx + CONTEXT_ESP], eax
    
    ; Save all registers
    mov [edx + CONTEXT_EAX], eax
    mov [edx + CONTEXT_EBX], ebx
    mov [edx + CONTEXT_ECX], ecx
    mov [edx + CONTEXT_EDX], edx
    mov [edx + CONTEXT_ESI], esi
    mov [edx + CONTEXT_EDI], edi
    mov [edx + CONTEXT_EBP], ebp
    
    ; Save EFLAGS
    pushfd
    pop eax
    mov [edx + CONTEXT_EFLAGS], eax
    
    ret

; Restore a CPU context from the provided structure and jump to its EIP.
;
; void process_restore_context(struct cpu_context *context);
;
; Called with the context pointer on the stack.
; Does not return; instead jumps to the saved EIP.
global process_restore_context
process_restore_context:
    mov eax, [esp + 4]           ; eax = context pointer
    
    ; Restore all general purpose registers
    mov ebx, [eax + CONTEXT_EBX]
    mov ecx, [eax + CONTEXT_ECX]
    mov edx, [eax + CONTEXT_EDX]
    mov esi, [eax + CONTEXT_ESI]
    mov edi, [eax + CONTEXT_EDI]
    mov ebp, [eax + CONTEXT_EBP]
    
    ; Restore EFLAGS
    mov ecx, [eax + CONTEXT_EFLAGS]
    push ecx
    popfd
    
    ; Restore ESP and jump to EIP
    mov esp, [eax + CONTEXT_ESP]
    mov ecx, [eax + CONTEXT_EIP]
    
    ; Restore EAX last (it was used as temporary)
    mov eax, [eax + CONTEXT_EAX]
    
    ; Jump to the saved instruction pointer
    jmp ecx

; Perform an actual context switch between processes.
;
; void process_context_switch(struct process *from, struct process *to);
;
; Saves the current process's context and restores the target process's context.
; This is called from process_schedule() to actually perform the switch.
global process_context_switch
process_context_switch:
    push ebp
    mov ebp, esp
    
    ; Parameters:
    ; [ebp + 8]  = from (struct process *)
    ; [ebp + 12] = to (struct process *)
    
    mov eax, [ebp + 8]    ; eax = from process
    mov edx, [ebp + 12]   ; edx = to process
    
    ; Save current process's context if from is not NULL
    test eax, eax
    jz .skip_save
    
    ; context is at offset 4 in struct process
    add eax, 4
    push eax
    call process_save_context
    add esp, 4
    
.skip_save:
    ; Restore the target process's context
    ; context is at offset 4 in struct process
    add edx, 4
    push edx
    call process_restore_context
    ; process_restore_context does not return
