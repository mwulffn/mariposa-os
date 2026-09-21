# Tasks and Scheduler Design

> **Status.** Implemented in `src/kernel/task.c`, `src/kernel/switch.s` and
> `src/kernel/vector.c`, pinned by the `task.*` tests, which run the real
> kernel image on the 68000 core and again on the 68020 core. The ROM detects
> the CPU and FPU and reports them in `struct bootinfo` version 2.

## Overview

Kernel threads. A task is a control block and a stack; everything shares one
address space and runs in supervisor mode. Scheduling is preemptive, with
four priority levels and round-robin inside each, driven by the 50Hz
vertical blank tick. Tasks block on sleep and on wait queues; interrupt
handlers wake them.

## Decisions

### Supervisor mode only - for now, and cheaply reversible

The 68000 has no MMU, so user mode on the target machine protects nothing
and costs a privilege transition on every kernel call. Isolation for
applications comes from the Wasm sandbox, which works on every CPU.

The 68030, 68040 and 68060 do have MMUs, and on those user mode plus
protection is worth having for native code. So the option is kept open
where keeping it open is nearly free:

- every switch leaves through `RTE`, so the S bit comes back from the saved
  SR like any other bit - nothing in the switch path assumes supervisor;
- the saved context includes USP.

A user-mode task is then a different initial SR and a system call entry, not
a redesign of the context frame.

### One frame, one way out

A suspended task's stack holds, from the saved SP upwards:

```
saved sp -> USP                4 bytes
            D0-D7, A0-A6      60 bytes
            exception frame    6 bytes on a 68000: SR, PC
                               8 bytes on 68010+:  SR, PC, format/vector word
```

There is exactly one frame format and exactly one restore path, whether the
task was preempted by an interrupt, yielded, blocked, or has never run:

- **Preemption.** Interrupt handlers never switch. They set `need_resched`
  and leave through `isr_exit`, which switches only when the interrupt is
  returning to task level (saved interrupt mask 0) - so a nested interrupt
  never switches out from under the one it interrupted.
- **Yield and block.** `TRAP #0`. The CPU builds the same exception frame an
  interrupt would, and the handler joins the same path unconditionally.
- **A new task.** `task_create` fabricates the frame by hand. This is the
  only code that must know the CPU's frame layout, and it is one function:
  `build_initial_frame`. Hardcoding six bytes there is the bug that would
  otherwise surface on the first 68020.

Yielding with interrupts masked is allowed and is what makes blocking
race-free: the caller masks, queues itself, marks itself blocked and traps.
The next task's SR comes from its own frame, and the blocked task gets its
mask back when it is resumed.

### Priorities

Four levels, a FIFO ready queue each; the highest non-empty queue runs.
`TASK_PRIO_IDLE` is the idle task's alone. A task runs for `TASK_QUANTUM`
ticks before the next at its level gets a turn; a task made ready at a higher
level preempts at the next interrupt exit, or at once if woken from task
context.

Priority exists from the start because the workload needs it: an input task
has to beat a Wasm VM that is busy computing.

### The boot context becomes the idle task

`sched_start()` does not fabricate an idle task; it adopts the context that
called it, on the stack the ROM reserved, and loops on `cpu_idle()` - which
is `STOP #$2000`, so an idle machine is actually halted. Zombies are reaped
here, with interrupts enabled.

### Blocking

```
task_sleep(ticks)
task_wait(&queue)             wake_one(&queue) / wake_all(&queue)
```

`wake_*` is the one scheduler call an interrupt handler may make. Sleepers
sit on a list the tick scans.

### Exit and cleanup

A task that returns from its entry function lands in `task_exit`. It cannot
free the stack it is standing on, so it marks itself dead, joins the zombie
list and switches away; the idle task and `task_create` reap zombies with
`mem_free_owner(task)`, which takes everything the task allocated and did
not free, and then free its stack.

The control block is not a separate allocation: it sits at the top of the
stack's. Above, because stacks grow down, so an overflow runs away from it.
The first version had them separate with the control block lower in memory,
and the overflow test found that the first thing a runaway stack destroyed
was the `stack_base` pointer the overflow check depends on.

Reaping happens only when the machine goes idle or someone creates a task.
A system that is never idle and never creates a task keeps its zombies;
that is a known limit, not an oversight.

### Stack overflow

There is no MMU to catch it and this project has already had one silent
stack corruption. Two cheap defences:

- a canary word at the bottom of every stack, checked at every switch; a
  dead canary panics with the task's name;
- stacks are filled with a pattern at creation, so `task_stack_unused()`
  reports the high-water mark.

## Running on more than the 68000

The target is the 68000, but nothing here should have to be redesigned to
run up to a 68060.

| Concern | 68000 | 68010 and later | What the code does |
|---|---|---|---|
| Exception frame | 6 bytes | 8+ bytes, format word | `RTE` handles its own; `build_initial_frame` is CPU-aware |
| Vector table | address 0 | wherever VBR points | all installs go through `vector_set()` |
| FPU state | none | 68881/2, or on-chip on 040/060; save frames of 4 to 200+ bytes | `struct task` has an `fpu_state` pointer, NULL until a task uses the FPU. Not implemented |
| User mode worth having | no | with an MMU, yes | USP saved, switch leaves by `RTE` |
| Atomicity | SR masking | same | `TAS` is broken on the Amiga bus and `CAS` is 68020+; neither is used |
| Second supervisor stack | none | 68020+ have MSP/ISP | the M bit is left clear |

The ROM detects the CPU and FPU (`src/rom/cpu_detect.s`) and reports them as
`cpu_type` and `fpu_type` in `struct bootinfo` version 2. A kernel handed a
version 1 struct assumes a 68000.

Not the scheduler's problem, but the same ambition: code that writes
instructions and then runs them (a hunk loader, a Wasm JIT) must flush the
instruction cache on a 68030 and up; the 68060 traps on a handful of integer
instructions (64-bit multiply and divide, `MOVEP`), which `-cpu=68000` C
never emits and the assembly here does not use; and the ROM's panic decoder
knows only the 68000's bus error frame.

## Printing from tasks

`kprintf` formats a line into a buffer on its own stack and hands it to
`ser_write` in one piece, which puts it into the transmit ring as a unit - so
two tasks printing at once cannot interleave inside a line (`aBBaaaBBaa`, in
the test that pins this). It works from handlers too. It costs the caller
about 500 bytes of stack.

A task that prints faster than the wire sleeps until the ring has drained;
see `docs/serial_design.md`, "ser_write", for why that took three attempts.

## Not in the first cut

- **A sleeping mutex.** `CRITICAL_ENTER` covers short sections. A lock that
  can be held across blocking arrives with its first real user, probably the
  filesystem.
- **FPU context.** The slot is reserved, nothing fills it.
- **Priority inheritance, dynamic priorities, per-task CPU accounting.**

## API

```
void          sched_init(unsigned long cpu_type);
void          sched_start(void);                  /* never returns */

struct task  *task_create(const char *name, void (*entry)(void *), void *arg,
                          unsigned long stack_size, int prio);
void          task_exit(void);
void          task_yield(void);
void          task_sleep(unsigned long ticks);
struct task  *task_current(void);
unsigned long task_stack_unused(const struct task *t);

void          task_wait(struct waitq *q);
void          wake_one(struct waitq *q);          /* ISR safe */
void          wake_all(struct waitq *q);          /* ISR safe */

unsigned long sched_ticks(void);
```
