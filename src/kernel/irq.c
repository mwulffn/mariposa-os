/*
 * irq.c - interrupt setup
 *
 * The handlers themselves are in isr.s. This is the wiring: where the
 * vector goes and which sources Paula is allowed to raise.
 */
#include "amiga_hw.h"
#include "irq.h"
#include "serial.h"
#include "vector.h"

/*
 * The ROM has already filled the table with handlers that panic, which is
 * the right default: an interrupt nobody installed for should be loud, not
 * silent. Installs go through vector_set(), which knows where the table is
 * on this CPU.
 */
extern void tick_handler(void);     /* switch.s */
extern void yield_handler(void);    /* switch.s */

void irq_init(void)
{
    /* Before anything can interrupt: from here on a crash flushes the
     * serial ring ahead of the ROM's panic dump. */
    trap_init(vector_table());

    vector_set(VEC_AUTOVECTOR(1), ser_tbe_handler);
    vector_set(VEC_AUTOVECTOR(3), tick_handler);
    vector_set(VEC_TRAP(0), yield_handler);

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
