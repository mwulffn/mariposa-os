/*
 * cpu.h - interrupt control
 *
 * See docs/interrupt_control_design.md. The rule that matters: never use
 * blind enable/disable pairs. A function that disables interrupts has no
 * idea whether its caller had them enabled, so restoring them blindly is
 * how a critical section leaks. Save and restore instead.
 */
#ifndef CPU_H
#define CPU_H

unsigned long cpu_sr_get(void);
void          cpu_sr_set(unsigned long sr);

/* Mask every level, and hand back the SR that was there before. */
unsigned long cpu_int_disable(void);

/* Supervisor, all levels unmasked. The one-way switch at startup; inside
 * the kernel use the critical section below instead. */
void          cpu_int_enable(void);

/* Halt until the next interrupt, with all levels unmasked. Returns after the
 * handler has run. For the idle loop and for waiting on an interrupt; never
 * inside a critical section, since it lowers the mask to 0. */
void          cpu_idle(void);

/* The vector base register. 68010 and later only - it is an illegal
 * instruction on a 68000. Use vector_set(), which knows. */
unsigned long cpu_vbr_get(void);

/*
 * Critical sections. Declares its own saved SR, so sections nest correctly
 * and an inner one cannot re-enable interrupts an outer one had masked.
 *
 *     CRITICAL_ENTER();
 *     ... shared state ...
 *     CRITICAL_EXIT();
 */
#define CRITICAL_ENTER()  { unsigned long _saved_sr = cpu_int_disable();
#define CRITICAL_EXIT()   cpu_sr_set(_saved_sr); }

#endif /* CPU_H */
