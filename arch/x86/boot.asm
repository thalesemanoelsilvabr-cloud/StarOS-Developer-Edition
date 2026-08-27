; StarOS boot.asm — Multiboot2 + VESA 800x600x32
; nasm -f elf32 boot.asm -o boot.o
MB2_MAGIC equ 0xE85250D6
MB2_ARCH  equ 0
section .multiboot2
align 8
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd (mb2_end - mb2_start)
    dd -(MB2_MAGIC + MB2_ARCH + (mb2_end - mb2_start))
    ; framebuffer tag
    align 8
    dw 5, 1
    dd 20
    dd 800, 600, 32
    ; end tag
    align 8
    dw 0, 0
    dd 8
mb2_end:

section .bss
align 16
stack_bot: resb 32768
stack_top:

section .text
global _start
extern kernel_main
_start:
    mov  esp, stack_top
    push ebx
    push eax
    call kernel_main
    cli
.halt: hlt
    jmp .halt
