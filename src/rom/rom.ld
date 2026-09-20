/*
 * rom.ld - 256KB Amiga kickstart image at $FC0000.
 *
 * The ROM was assembled straight to a raw binary with `org` until C entered
 * the picture; linking is what lets vbcc objects join in. The output is
 * byte-identical to the org-based build for the same assembly input, so the
 * switch itself changed nothing in the image.
 *
 * .text must come first: the CPU reads the initial SSP and PC from the first
 * eight bytes of ROM, and bootstrap.s puts the ROM header at the top of it.
 */
SECTIONS
{
    . = 0xFC0000;

    .text : {
        *(.text)
        *(CODE)
    }

    .rodata : { *(.rodata) }

    /* vbcc emits string literals and initialised data into DATA. Anything
     * landing here is read-only in practice - it lives in ROM. C in this
     * image must not expect writable statics; scratch goes in chip RAM. */
    .data : {
        *(DATA)
        *(.data)
    }

    /* A pointer back to the start of the ROM, then the 16-byte interrupt
     * vector number table that must be the very end of the image - see
     * iack_vector_table in bootstrap.s. */
    . = 0xFFFFEC;
    .romend : { *(.romend) }
}
