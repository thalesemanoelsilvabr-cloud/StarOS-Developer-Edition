/* linker.ld — StarOS x86 32-bit */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS {
    . = 1M;

    .text ALIGN(4) : {
        *(.text)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
