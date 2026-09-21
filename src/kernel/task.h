/*
 * task.h - kernel threads and the scheduler
 *
 * See docs/task_design.md.
 */
#ifndef TASK_H
#define TASK_H

#define TASK_PRIO_IDLE    0     /* the idle task's alone */
#define TASK_PRIO_LOW     1
#define TASK_PRIO_NORMAL  2
#define TASK_PRIO_HIGH    3
#define TASK_NPRIO        4

#define TASK_QUANTUM      2     /* ticks before the next at this level runs */
#define TASK_MIN_STACK    512

#define TASK_READY        0
#define TASK_RUNNING      1
#define TASK_SLEEPING     2
#define TASK_WAITING      3
#define TASK_DEAD         4

struct task {
    void          *sp;          /* saved stack pointer. switch.s knows this
                                 * is first */
    struct task   *next;        /* whichever one list the task is on: a ready
                                 * queue, a wait queue, sleepers or zombies */
    unsigned char  state;
    unsigned char  prio;
    short          quantum;     /* ticks left in this turn */
    unsigned long  wake_tick;   /* when a sleeper is due */
    unsigned long *stack_base;  /* lowest address; holds the canary. NULL for
                                 * the idle task, whose stack the ROM made */
    unsigned long  stack_size;
    void          *fpu_state;   /* reserved: NULL until a task uses the FPU */
    const char    *name;
    struct task   *all_next;    /* every task there is, for task_next() */
};

struct waitq {
    struct task *head;
    struct task *tail;
};

/* cpu_type is CPU_* from bootinfo.h. Call before irq_init(). */
void sched_init(unsigned long cpu);

/* Turn the caller into the idle task and start scheduling. Interrupts must
 * be enabled. Never returns. */
void sched_start(void);

/* prio is TASK_PRIO_LOW..HIGH. Returns NULL if there is no memory or the
 * arguments make no sense. entry returning is the same as task_exit(). */
struct task *task_create(const char *name, void (*entry)(void *), void *arg,
                         unsigned long stack_size, int prio);
void         task_exit(void);

/* Call hook(task) whenever a task exits, in that task's own context, before
 * it is marked dead. For subsystems that hand out things a task can die
 * holding - the display is the first - so that task.c need not know about
 * any of them. Registering the same hook twice is harmless. -1 if full. */
int          task_on_exit(void (*hook)(struct task *t));
void         task_yield(void);
void         task_sleep(unsigned long ticks);
struct task *task_current(void);

/* Walk every task, the idle task included: pass NULL for the first. Hold a
 * critical section across the walk - tasks come and go. */
struct task *task_next(const struct task *t);

/* Bytes of stack the task has never touched. */
unsigned long task_stack_unused(const struct task *t);

/* Block until woken. wake_* may be called from an interrupt handler, and
 * are the only calls here that may. */
void task_wait(struct waitq *q);
void wake_one(struct waitq *q);
void wake_all(struct waitq *q);

/*
 * A lock that may be held across a task switch. CRITICAL_ENTER is not one:
 * it stops being a lock the moment the holder sleeps, and it holds off every
 * interrupt in the machine while it lasts. Use this for anything long, or
 * anything that waits - a disk transfer, a filesystem operation.
 *
 * Task context only, not recursive, no priority inheritance. Before the
 * scheduler starts there is nobody to contend with and it always succeeds.
 * Zero-initialised is unlocked.
 */
struct mutex {
    struct task *owner;
    struct waitq waiters;
};

void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);

unsigned long sched_ticks(void);

/* May the caller sleep? Only a task can: not a handler, and not the kernel
 * before sched_start. It says nothing about the caller's interrupt mask - a
 * caller inside its own critical section must not sleep either, and only it
 * knows. */
int sched_can_block(void);

/* For switch.s. */
void  sched_tick(void);
void *sched_switch(void *sp);

#endif /* TASK_H */
