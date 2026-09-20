/*
 * irq.c - interrupt setup
 *
 * The handlers themselves are in isr.s. This is the wiring: where the
 * vector goes and which sources Paula is allowed to raise.
 */
#include "amiga_hw.h"
#include "irq.h"
#include "serial.h"

/*
 * Level 3 autovector. The 68000 has no VBR - that is a 68010 register - so
 * the vector table is at address 0 and nowhere else, and this is simply
 * where the chip looks. The ROM has already filled the table with handlers
 * that panic, which is the right default: an interrupt nobody installed for
 * should be loud, not silent.
 */
#define VEC_AUTOVECTOR_1  ((volatile void **)0x64UL)
#define VEC_AUTOVECTOR_3  ((volatile void **)0x6CUL)

extern void vbl_handler(void);

void irq_init(void)
{
    /* Before anything can interrupt: from here on a crash flushes the
     * serial ring ahead of the ROM's panic dump. */
    trap_init();

    *VEC_AUTOVECTOR_1 = (void *)ser_tbe_handler;
    *VEC_AUTOVECTOR_3 = (void *)vbl_handler;

    /*
     * Clear every pending request before enabling anything, or a request
     * left over from the ROM's boot would fire the moment the gate opens.
     * Bit 15 clear is the CLR direction, so $7FFF clears all fourteen.
     */
    custom.intreq = 0x7FFF;

    /*
     * Open Paula's gate: the master enable and vertical blank. Bit 15 set
     * is the SET direction. Nothing arrives yet - SR still masks it - until
     * the caller runs cpu_int_enable().
     */
    custom.intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;

    /* Last, because it has to come after the INTREQ clear above: that may
     * have thrown away the TBE request a queued byte was waiting on, and
     * ser_irq_enable restarts the transmitter rather than trusting it. */
    ser_irq_enable();
}
