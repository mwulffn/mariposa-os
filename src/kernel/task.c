/*
 * task.c - kernel threads and the scheduler
 *
 * See docs/task_design.md for the decisions; this is the mechanism. The
 * context switch itself is in switch.s, which calls sched_switch() with the
 * outgoing task's stack pointer and resumes on whatever comes back.
 *
 * Every queue here is touched from tasks and from interrupt handlers, so
 * everything runs with interrupts masked: inside CRITICAL_ENTER, or called
 * from switch.s, which masks first.
 */

#include "task.h"
#include "mem.h"
#include "cpu.h"
#include "vector.h"
#include "serial.h"
#include "kprintf.h"
#include "bootinfo.h"

#define CANARY      0x5354414BUL    /* 'STAK' */
#define STACK_FILL  0xA5A5A5A5UL

extern void (*rom_panic)(void);
extern volatile unsigned char need_resched;
extern volatile unsigned char isr_depth;
extern volatile unsigned long vbl_count;

static struct waitq ready[TASK_NPRIO];
static struct task *current;
static struct task *sleepers;
static struct task *zombies;
static struct task *all_tasks;
static struct task  idle_task;

#define MAX_EXIT_HOOKS 4
static void (*exit_hooks[MAX_EXIT_HOOKS])(struct task *);
static int          started;

/* --------------------------------------------------------------- queues --- */

static void q_push(struct waitq *q, struct task *t)
{
    t->next = 0;
    if (q->tail)
        q->tail->next = t;
    else
        q->head = t;
    q->tail = t;
}

static struct task *q_pop(struct waitq *q)
{
    struct task *t = q->head;

    if (t) {
        q->head = t->next;
        if (!q->head)
            q->tail = 0;
        t->next = 0;
    }
    return t;
}

/* Make a task runnable, and ask for a switch if it outranks whoever is
 * running. Interrupts masked. */
static void make_ready(struct task *t)
{
    t->state = TASK_READY;
    q_push(&ready[t->prio], t);
    if (started && t->prio > current->prio)
        need_resched = 1;
}

/* ----------------------------------------------------------------- init --- */

void sched_init(unsigned long cpu)
{
    int i;

    cpu_type = cpu;
    for (i = 0; i < TASK_NPRIO; i++)
        ready[i].head = ready[i].tail = 0;
    current = 0;
    sleepers = 0;
    zombies = 0;
    all_tasks = 0;
    started = 0;
    need_resched = 0;
}

unsigned long sched_ticks(void)
{
    return vbl_count;
}

int sched_can_block(void)
{
    return started && isr_depth == 0;
}

struct task *task_current(void)
{
    return current;
}

/* --------------------------------------------------------------- create --- */

/*
 * Fabricate the frame a task would have if it had been running and was
 * interrupted at its first instruction. The ONLY code that knows what an
 * exception frame looks like - everything else lets the CPU build them and
 * RTE take them apart. Built downwards from `top`; returns the saved SP.
 *
 * Above the frame, where the task finds them once RTE has consumed it: a
 * return address and the argument, as if task_exit had called entry(arg).
 */
static void *build_initial_frame(unsigned long *top, void (*entry)(void *),
                                 void *arg)
{
    unsigned short *w;
    unsigned long *l = top;
    int i;

    *--l = (unsigned long)arg;
    *--l = (unsigned long)task_exit;

    w = (unsigned short *)l;
    if (cpu_type >= CPU_68010)
        *--w = 0x0000;                  /* format 0, vector offset unused */
    *--w = (unsigned short)((unsigned long)entry & 0xFFFF);
    *--w = (unsigned short)((unsigned long)entry >> 16);
    *--w = 0x2000;                      /* SR: supervisor, nothing masked */

    l = (unsigned long *)w;
    for (i = 0; i < 15; i++)
        *--l = 0;                       /* D0-D7, A0-A6 */
    *--l = 0;                           /* USP */
    return l;
}

static void reap(void)
{
    for (;;) {
        struct task *t;

        struct task **link;

        CRITICAL_ENTER();
        t = zombies;
        if (t) {
            zombies = t->next;
            for (link = &all_tasks; *link; link = &(*link)->all_next)
                if (*link == t) {
                    *link = t->all_next;
                    break;
                }
        }
        CRITICAL_EXIT();
        if (!t)
            return;

        /* Whatever it allocated and forgot, then the thing it could not
         * free for itself because it was standing on it: the stack, which
         * has the control block at its top. */
        mem_free_owner(t);
        mem_free(t->stack_base);
    }
}

struct task *task_create(const char *name, void (*entry)(void *), void *arg,
                         unsigned long stack_size, int prio)
{
    struct task *t;
    unsigned long *base;
    unsigned long i, words;

    if (prio <= TASK_PRIO_IDLE || prio >= TASK_NPRIO || !entry)
        return 0;
    if (stack_size < TASK_MIN_STACK)
        stack_size = TASK_MIN_STACK;
    stack_size = (stack_size + 3) & ~3UL;

    reap();                             /* memory may be waiting here */

    /*
     * One allocation: the stack, with the control block above it. Above,
     * because stacks grow down - an overflow runs away from the control
     * block, not through it. The first version of this put them in separate
     * allocations with the control block lower, and the overflow test found
     * that the first thing a runaway stack destroyed was the stack_base
     * pointer the overflow check depends on.
     */
    base = mem_alloc(stack_size + sizeof *t, ALLOC_ANY);
    if (!base)
        return 0;
    t = (struct task *)((char *)base + stack_size);
    t->stack_base = base;

    words = stack_size / 4;
    t->stack_base[0] = CANARY;
    for (i = 1; i < words; i++)
        t->stack_base[i] = STACK_FILL;

    t->stack_size = stack_size;
    t->sp         = build_initial_frame(t->stack_base + words, entry, arg);
    t->prio       = (unsigned char)prio;
    t->quantum    = TASK_QUANTUM;
    t->wake_tick  = 0;
    t->fpu_state  = 0;
    t->name       = name;

    CRITICAL_ENTER();
    t->all_next = all_tasks;
    all_tasks = t;
    make_ready(t);
    CRITICAL_EXIT();
    return t;
}

struct task *task_next(const struct task *t)
{
    return t ? t->all_next : all_tasks;
}

unsigned long task_stack_unused(const struct task *t)
{
    unsigned long i, words;

    if (!t->stack_base)
        return 0;
    words = t->stack_size / 4;
    for (i = 1; i < words && t->stack_base[i] == STACK_FILL; i++)
        ;
    return (i - 1) * 4;
}

/* --------------------------------------------------------------- switch --- */

static void stack_overflow(struct task *t)
{
    pr_emerg("\nKERNEL: stack overflow in task '%s' (stack $%08lx-$%08lx, "
             "sp $%08lx)\n", t->name, (unsigned long)t->stack_base,
             (unsigned long)t->stack_base + t->stack_size,
             (unsigned long)t->sp);
    ser_flush();
    rom_panic();
}

/* Called by switch.s, interrupts masked, on the outgoing task's stack. */
void *sched_switch(void *sp)
{
    struct task *t = current;
    int p;

    t->sp = sp;
    need_resched = 0;

    if (t->stack_base &&
        (t->stack_base[0] != CANARY || (unsigned long *)sp <= t->stack_base))
        stack_overflow(t);

    if (t->state == TASK_RUNNING) {
        t->state = TASK_READY;
        q_push(&ready[t->prio], t);
    }

    /* The idle task is always ready or running, so this always finds one. */
    for (p = TASK_NPRIO - 1; p >= 0; p--)
        if (ready[p].head)
            break;
    t = q_pop(&ready[p]);

    t->state   = TASK_RUNNING;
    t->quantum = TASK_QUANTUM;
    current = t;
    return t->sp;
}

/* Called by the tick handler, interrupts masked. */
void sched_tick(void)
{
    struct task **link, *t;

    if (!started)
        return;

    for (link = &sleepers; (t = *link) != 0; ) {
        if ((long)(vbl_count - t->wake_tick) >= 0) {
            *link = t->next;
            make_ready(t);
        } else {
            link = &t->next;
        }
    }

    if (--current->quantum <= 0)
        need_resched = 1;
}

/* ------------------------------------------------------------- blocking --- */

void task_sleep(unsigned long ticks)
{
    if (ticks == 0) {
        task_yield();
        return;
    }
    CRITICAL_ENTER();
    current->wake_tick = vbl_count + ticks;
    current->state = TASK_SLEEPING;
    current->next = sleepers;
    sleepers = current;
    task_yield();
    CRITICAL_EXIT();
}

void task_wait(struct waitq *q)
{
    CRITICAL_ENTER();
    current->state = TASK_WAITING;
    q_push(q, current);
    task_yield();
    CRITICAL_EXIT();
}

/* A task made ready from a handler is switched to by isr_exit. From a task
 * there is no isr_exit coming, so go now rather than at the next tick. */
static void resched_if_task_level(void)
{
    if (need_resched && started && isr_depth == 0)
        task_yield();
}

void wake_one(struct waitq *q)
{
    struct task *t;

    CRITICAL_ENTER();
    t = q_pop(q);
    if (t)
        make_ready(t);
    CRITICAL_EXIT();
    resched_if_task_level();
}

void wake_all(struct waitq *q)
{
    struct task *t;

    CRITICAL_ENTER();
    while ((t = q_pop(q)) != 0)
        make_ready(t);
    CRITICAL_EXIT();
    resched_if_task_level();
}

/* ---------------------------------------------------------------- mutex --- */

/* Who holds it before there are tasks: anything that is not NULL. */
#define BOOT_OWNER ((struct task *)1)

void mutex_lock(struct mutex *m)
{
    struct task *me = sched_can_block() ? current : BOOT_OWNER;

    CRITICAL_ENTER();
    /* A loop: being woken means the lock was free a moment ago, not that
     * it still is - someone running may have taken it first. */
    while (m->owner && sched_can_block())
        task_wait(&m->waiters);
    m->owner = me;
    CRITICAL_EXIT();
}

void mutex_unlock(struct mutex *m)
{
    CRITICAL_ENTER();
    m->owner = 0;
    CRITICAL_EXIT();
    wake_one(&m->waiters);
}

int task_on_exit(void (*hook)(struct task *t))
{
    int i, rc = -1;

    CRITICAL_ENTER();
    for (i = 0; i < MAX_EXIT_HOOKS; i++)
        if (exit_hooks[i] == hook || !exit_hooks[i]) {
            exit_hooks[i] = hook;
            rc = 0;
            break;
        }
    CRITICAL_EXIT();
    return rc;
}

void task_exit(void)
{
    int i;

    /* While this is still a live task with interrupts on: a hook may need
     * to do real work. Memory is not their business - the reaper frees
     * whatever the task owned - but things like the display have to be
     * given back now, not whenever the machine next goes idle. */
    for (i = 0; i < MAX_EXIT_HOOKS; i++)
        if (exit_hooks[i])
            exit_hooks[i](current);

    cpu_int_disable();
    current->state = TASK_DEAD;
    current->next = zombies;
    zombies = current;
    task_yield();
    /* not reached: nothing will ever make a dead task ready */
    for (;;)
        ;
}

/* ---------------------------------------------------------------- start --- */

void sched_start(void)
{
    cpu_int_disable();
    idle_task.name       = "idle";
    idle_task.prio       = TASK_PRIO_IDLE;
    idle_task.state      = TASK_RUNNING;
    idle_task.quantum    = TASK_QUANTUM;
    idle_task.stack_base = 0;
    idle_task.fpu_state  = 0;
    idle_task.all_next   = all_tasks;
    all_tasks = &idle_task;
    current = &idle_task;
    started = 1;
    cpu_int_enable();

    /* This context is the idle task from here on. Whatever was created
     * before now outranks it, so step aside once; after that every wakeup
     * is an interrupt, and isr_exit does the switching. */
    task_yield();
    for (;;) {
        reap();
        cpu_idle();
    }
}
