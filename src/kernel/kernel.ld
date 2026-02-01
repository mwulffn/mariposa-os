/* kernel.ld - vlink linker script for kernel */

SECTIONS {
    . = 0x200000;

    .text: {
        *(.text)
        *(CODE)
    }

    .rodata: {
        *(.rodata)
    }

    .data: {
        *(.data)
        *(DATA)
    }

    .bss: {
        __bss_start = .;
        *(.bss)
        *(BSS)
        *(COMMON)
        __bss_end = .;
    }

    __end = .;
}
