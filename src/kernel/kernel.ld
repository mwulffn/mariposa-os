SECTIONS
{
    . = 0x200000;
    __text_start = .;

    .text : {
        *(.text)
        *(CODE)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
        *(DATA)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(BSS)
        *(COMMON)
        __bss_end = .;
    }

    __end = .;
}
