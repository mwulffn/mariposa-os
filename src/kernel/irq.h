/*
 * irq.h - interrupt setup
 */
#ifndef IRQ_H
#define IRQ_H

/*
 * Install the handlers, clear anything stale, and open Paula's gate.
 *
 * This does NOT open the CPU's gate: SR still masks everything when this
 * returns. Both gates have to be open for an interrupt to arrive, and the
 * caller lowers SR with cpu_int_enable() once it is ready to be
 * interrupted. See docs/interrupt_control_design.md.
 */
void irq_init(void);

/* Vertical blanks seen since boot. Written by the ISR, so volatile. */
extern volatile unsigned long vbl_count;

#endif /* IRQ_H */
