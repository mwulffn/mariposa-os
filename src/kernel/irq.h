/*
 * irq.h - interrupt sources, shared levels and their dispatch
 *
 * See docs/driver_design.md. Paula folds fourteen sources onto six CPU
 * levels, so handlers attach to a source, not to a vector.
 */
#ifndef IRQ_H
#define IRQ_H

/* Sources are INTENA/INTREQ bit numbers. */
#define IRQ_TBE       0     /* level 1 - serial transmit buffer empty */
#define IRQ_DSKBLK    1     /* level 1 - disk block done */
#define IRQ_SOFTINT   2     /* level 1 - software interrupt */
#define IRQ_PORTS     3     /* level 2 - CIA-A: keyboard, timers */
#define IRQ_COPER     4     /* level 3 - copper */
#define IRQ_VERTB     5     /* level 3 - vertical blank */
#define IRQ_BLIT      6     /* level 3 - blitter done */
#define IRQ_AUD0      7     /* level 4 */
#define IRQ_AUD1      8     /* level 4 */
#define IRQ_AUD2      9     /* level 4 */
#define IRQ_AUD3      10    /* level 4 */
#define IRQ_RBF       11    /* level 5 - serial receive buffer full */
#define IRQ_DSKSYNC   12    /* level 5 - disk sync found */
#define IRQ_EXTER     13    /* level 6 - CIA-B */
#define IRQ_NSOURCES  14

/* Install the vectors and the kernel's own handlers, open Paula's master
 * enable. Nothing arrives until the caller lowers SR. Call sched_init()
 * first: installing a vector needs to know the CPU. */
void irq_init(void);

/*
 * Attach a handler to a source. It runs with every interrupt masked and
 * must acknowledge its own source - how and when is device knowledge the
 * dispatcher does not have. It may wake tasks and start the device's next
 * job; it may not block, allocate or call another driver. Attaching does
 * not enable.
 */
void irq_attach(unsigned int source, void (*handler)(void *), void *arg);

/* The only code that writes INTENA. */
void irq_enable(unsigned int source);
void irq_disable(unsigned int source);

/* Called by switch.s. */
void irq_dispatch(unsigned long level);

/* Sources that fired with nobody attached, and were switched off for it. */
extern volatile unsigned long irq_spurious;

/* Vertical blanks seen since boot. */
extern volatile unsigned long vbl_count;

/* Wrap the fault vectors so a crash flushes serial first. In vectors.s.
 * table is vector_table(): address 0 on a 68000, VBR after it. */
void trap_init(void **table);

#endif /* IRQ_H */
