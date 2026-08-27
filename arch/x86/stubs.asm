; stubs.asm — GDT/IDT flush + ISR/IRQ stubs
[BITS 32]
global gdt_flush, idt_flush

gdt_flush:
    mov eax,[esp+4]
    lgdt [eax]
    mov ax,0x10
    mov ds,ax; mov es,ax; mov fs,ax; mov gs,ax; mov ss,ax
    jmp 0x08:.flush
.flush: ret

idt_flush:
    mov eax,[esp+4]
    lidt [eax]
    ret

; Macro ISR sem erro
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

; Macro ISR com erro
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; Macro IRQ
%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 6
ISR_ERRCODE   8
ISR_ERRCODE   13
ISR_ERRCODE   14

IRQ 0,  32
IRQ 1,  33
IRQ 10, 42
IRQ 12, 44

extern isr_handler
extern irq_handler

isr_common:
    pusha
    mov ax,ds
    push eax
    mov ax,0x10
    mov ds,ax; mov es,ax; mov fs,ax; mov gs,ax
    push esp
    call isr_handler
    pop eax
    pop eax
    mov ds,ax; mov es,ax; mov fs,ax; mov gs,ax
    popa
    add esp,8
    iret

irq_common:
    pusha
    mov ax,ds
    push eax
    mov ax,0x10
    mov ds,ax; mov es,ax; mov fs,ax; mov gs,ax
    push esp
    call irq_handler
    pop eax
    pop eax
    mov ds,ax; mov es,ax; mov fs,ax; mov gs,ax
    popa
    add esp,8
    iret
