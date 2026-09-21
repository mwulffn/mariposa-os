/*
 * cia.h - CIA-A's interrupt control register, and its one owner
 *
 * CIA-A puts five interrupt sources behind one register: timer A, timer B,
 * the TOD alarm, the serial port (the keyboard) and the FLAG pin. Reading
 * ICR returns the pending flags and clears ALL of them - so a keyboard
 * driver that read it would silently acknowledge the timers, and a timer
 * driver that read it would swallow keystrokes. docs/driver_design.md's rule
 * for a register shared between devices is that it has one owner. This is
 * it: cia.c reads ICR, once per interrupt, and hands each flag to whoever
 * attached to it.
 */
#ifndef CIA_H
#define CIA_H

#define CIA_TIMER_A  0
#define CIA_TIMER_B  1
#define CIA_ALARM    2
#define CIA_SERIAL   3      /* the keyboard */
#define CIA_FLAG     4
#define CIA_NSOURCES 5

/* E clock ticks per second: the CIA timers count these. PAL; an NTSC
 * machine's is 715909, close enough for every use here. */
#define CIA_E_HZ     709379UL

/* Microseconds to E ticks, rounded up. Compile-time for a constant. */
#define CIA_USEC(us) ((unsigned short)(((us) * (CIA_E_HZ / 1000UL) + 999999UL) / 1000000UL + 1))

/* Mask every CIA-A source and take over IRQ_PORTS. After irq_init(). */
void cia_init(void);

/* As irq_attach: handlers run with everything masked. The flag has already
 * been acknowledged - reading ICR did that - so a handler acknowledges
 * nothing. Attaching does not enable. */
void ciaa_attach(unsigned int source, void (*handler)(void *), void *arg);
void ciaa_enable(unsigned int source);
void ciaa_disable(unsigned int source);

/* Run timer A once for `ticks` E ticks; CIA_TIMER_A fires when it is done. */
void ciaa_timer_a_oneshot(unsigned short ticks);

#endif /* CIA_H */
