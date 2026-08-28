; StarOS boot.asm — Multiboot2 para GRUB
; nasm -f elf32 boot.asm -o boot.o

BITS 32

; ── Constantes Multiboot2 ─────────────────────────────────────
MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0           ; i386
MB2_LEN      equ (header_end - header_start)
MB2_CHECKSUM equ (0x100000000 - (MB2_MAGIC + MB2_ARCH + MB2_LEN))

; ── Header Multiboot2 — DEVE estar em .text para o LD não descartar
section .text
global _start
extern kernel_main

; O header precisa estar ANTES de _start e alinhado em 8 bytes
align 8
header_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_LEN
    dd MB2_CHECKSUM

    ; tag: framebuffer 800x600x32
    align 8
    dw 5            ; type = framebuffer
    dw 1            ; flags = optional
    dd 20           ; size
    dd 800          ; width
    dd 600          ; height
    dd 32           ; bpp

    ; tag: end
    align 8
    dw 0            ; type = end
    dw 0            ; flags
    dd 8            ; size
header_end:

; ── Stack ─────────────────────────────────────────────────────
section .bss
align 16
stack_bottom:
    resb 32768
stack_top:

; ── Entry point ───────────────────────────────────────────────
section .text
_start:
    cli
    mov  esp, stack_top
    push ebx        ; mb2_info pointer
    push eax        ; mb2 magic
    call kernel_main
    cli
.halt:
    hlt
    jmp .halt