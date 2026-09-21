/*
 * test_task.c - src/kernel/task.c and switch.s: tasks and the scheduler
 *
 * Real tasks on the real kernel image. The bodies are in guest/ktasks.s;
 * each reports progress through a counter in a block the test owns, so a
 * test is: build some tasks, let the machine run for a while, look at the
 * counters.
 *
 * Every test runs on the 68000 core and again on the 68020 core. What that
 * buys is the exception frame - six bytes against eight with a format word -
 * which task_create has to fabricate by hand for a task that has never run.
 * Get it wrong and the first RTE into a new task on a 68020 takes a format
 * error, which the harness reports as a guest exception.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

#define CPU_68000 0u
#define CPU_68010 1u
#define CPU_68020 2u
#define CPU_68040 4u

#define PRIO_LOW     1u
#define PRIO_NORMAL  2u
#define PRIO_HIGH    3u

#define HEAP_BASE   0x210000u
#define HEAP_SIZE   0x040000u
#define ALLOC_FAST  2u

/* The boot context becomes the idle task, so it needs a stack of its own:
 * H_STACK_TOP is where h_call runs the test's own calls into the kernel. */
#define IDLE_STACK  0x2E0000u

#define TICK        20011u      /* cycles between vertical blanks; prime, so
                                 * ticks drift across the task bodies */
#define SLICE       4000000u    /* one look at the machine: ~200 ticks */

/* block layout, shared with guest/ktasks.s */
#define B_COUNT  0
#define B_FN     4
#define B_PARAM  8
#define B_AUX    12
#define B_FN2    16

static uint32_t kcall(const char *name, int nargs, const uint32_t *args)
{
    h_result r;
    int i;

    h_begin_call();
    for (i = nargs - 1; i >= 0; i--)
        h_push32(args[i]);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void setup(int model, uint32_t cpu_type)
{
    uint8_t map[24];
    uint32_t a[2];

    if (model != 68000)
        h_set_cpu(model);

    memset(map, 0, sizeof map);
    map[1] = (uint8_t)(HEAP_BASE >> 16);
    map[5] = (uint8_t)(HEAP_SIZE >> 16);
    map[9] = 2;                                 /* MEM_TYPE_FAST */
    a[0] = h_alloc(map, sizeof map);
    a[1] = 0;
    kcall("kernel:_mem_init", 2, a);

    /* crt0 normally fills this in from the handoff. */
    h_poke32(h_sym("kernel:_rom_panic"), h_sym("debugger_entry"));

    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_sched_init", 1, &cpu_type);
    kcall("kernel:_irq_init", 0, NULL);
    kcall("kernel:_input_init", 0, NULL);
    kcall("kernel:_cia_init", 0, NULL);
    kcall("kernel:_kbd_init", 0, NULL);
    h_vbl_every(TICK);
}

static uint32_t block(const char *fn, uint32_t param, uint32_t aux, const char *fn2)
{
    uint8_t zero[0x80];
    uint32_t b;

    memset(zero, 0, sizeof zero);
    b = h_alloc(zero, sizeof zero);
    if (fn)  h_poke32(b + B_FN, h_sym(fn));
    if (fn2) h_poke32(b + B_FN2, h_sym(fn2));
    h_poke32(b + B_PARAM, param);
    h_poke32(b + B_AUX, aux);
    return b;
}

static uint32_t spawn(const char *name, const char *body, uint32_t blk,
                      uint32_t stack, uint32_t prio)
{
    uint32_t a[5];
    a[0] = h_str(name); a[1] = h_sym(body); a[2] = blk; a[3] = stack; a[4] = prio;
    return kcall("kernel:_task_create", 5, a);
}

/* Let the machine run. The scheduler never returns, so the only good way
 * for a slice to end is by running out of budget. */
static int g_started;

static void run(uint64_t cycles)
{
    h_result r;

    h_set_cycle_budget(cycles);
    if (!g_started) {
        g_started = 1;
        h_set_sp(IDLE_STACK);
        h_set_sr(0x2000);
        r = h_run(h_sym("kernel:_sched_start"));
    } else {
        r = h_resume();
    }
    CHECK(r.status == H_TIMEOUT, "scheduler stopped: %s", r.detail);
}

static uint32_t count(uint32_t blk) { return h_peek32(blk + B_COUNT); }
static uint32_t ticks(void)         { return h_peek32(h_sym("kernel:_vbl_count")); }

/* --- the tests, each taking the CPU to run on ---------------------------- */

static void yield_alternates(int model, uint32_t cpu)
{
    uint32_t a, b;

    setup(model, cpu);
    h_vbl_every(0);                             /* cooperation only */
    a = block("kernel:_task_yield", 0, 0, NULL);
    b = block("kernel:_task_yield", 0, 0, NULL);
    CHECK(spawn("a", "body_yielder", a, 1024, PRIO_NORMAL) != 0, "create failed");
    CHECK(spawn("b", "body_yielder", b, 1024, PRIO_NORMAL) != 0, "create failed");
    run(400000);

    CHECK(count(a) > 20, "task a ran %u laps", count(a));
    CHECK(count(a) - count(b) + 1 <= 2,         /* within one of each other */
          "not alternating: a=%u b=%u", count(a), count(b));
}

static void tick_preempts(int model, uint32_t cpu)
{
    uint32_t a, b, ca, cb;

    setup(model, cpu);
    a = block(NULL, 0, 0, NULL);
    b = block(NULL, 0, 0, NULL);
    spawn("a", "body_spinner", a, 1024, PRIO_NORMAL);
    spawn("b", "body_spinner", b, 1024, PRIO_NORMAL);
    run(SLICE);

    ca = count(a); cb = count(b);
    CHECK(ca > 0 && cb > 0, "a spinner starved: a=%u b=%u", ca, cb);
    CHECK(ca < 2 * cb && cb < 2 * ca, "unfair split: a=%u b=%u", ca, cb);
}

/*
 * What a tick costs. Every interrupt now goes through a C dispatcher, and
 * that is worth its price only while the price is known: measured as the
 * work a lone spinner loses per tick. At 50Hz a tick is ~141,800 cycles
 * apart, so 4000 is under 3% of the machine.
 */
static void tick_cost_is_bounded(int model, uint32_t cpu)
{
    uint32_t a, quiet, ticked, lost, per_tick, n;

    setup(model, cpu);
    h_vbl_every(0);
    a = block(NULL, 0, 0, NULL);
    spawn("spin", "body_spinner", a, 1024, PRIO_NORMAL);
    run(200000);                                /* settle in */

    quiet = count(a);  run(2000000);  quiet = count(a) - quiet;

    h_vbl_every(TICK);
    n = ticks();
    ticked = count(a); run(2000000);  ticked = count(a) - ticked;
    n = ticks() - n;

    CHECK(n > 50, "only %u ticks", n);
    lost = quiet - ticked;                      /* laps; a lap is 2000000/quiet cycles */
    per_tick = (uint32_t)((uint64_t)lost * 2000000u / quiet / n);
    CHECK(per_tick < 4000, "a tick costs about %u cycles", per_tick);
    CHECK(per_tick > 200, "implausibly cheap tick: %u cycles", per_tick);
}

/* Preemption lands mid-instruction-stream at arbitrary points, and every
 * register has to come back - to the right task. */
static void registers_survive(int model, uint32_t cpu)
{
    uint32_t a, b, c;

    setup(model, cpu);
    h_vbl_every(3001);
    a = block(NULL, 0x11110000u, 0, NULL);
    b = block(NULL, 0x22220000u, 0, NULL);
    c = block("kernel:_task_yield", 0, 0, NULL);
    spawn("regs-a", "body_regs", a, 1024, PRIO_NORMAL);
    spawn("regs-b", "body_regs", b, 1024, PRIO_NORMAL);
    spawn("yield",  "body_yielder", c, 1024, PRIO_NORMAL);
    run(SLICE);

    CHECK(count(a) > 100 && count(b) > 100, "did not run: a=%u b=%u", count(a), count(b));
    CHECK_U32(0, h_peek32(a + B_AUX));
    CHECK_U32(0, h_peek32(b + B_AUX));
}

/*
 * An interrupt only switches tasks when it is returning to task level. Code
 * running with the mask raised is, as far as the scheduler can tell, inside
 * something - today a critical section, tomorrow a handler running at its
 * own level - and switching out from under it finishes that something on
 * another task's time.
 */
static void no_switch_above_task_level(int model, uint32_t cpu)
{
    uint32_t a, b;

    setup(model, cpu);
    a = block(NULL, 0, 0, NULL);
    b = block(NULL, 0, 0, NULL);
    spawn("masked", "body_masked_spinner", a, 1024, PRIO_NORMAL);
    spawn("other",  "body_spinner", b, 1024, PRIO_NORMAL);
    run(SLICE);

    CHECK(ticks() > 100, "the tick must still be delivered: %u", ticks());
    CHECK(count(a) > 0, "masked task never ran");
    CHECK_U32(0, count(b));
}

static void priority_wins(int model, uint32_t cpu)
{
    uint32_t hi, lo;

    setup(model, cpu);
    hi = block(NULL, 0, 0, NULL);
    lo = block(NULL, 0, 0, NULL);
    spawn("lo", "body_spinner", lo, 1024, PRIO_NORMAL);
    spawn("hi", "body_spinner", hi, 1024, PRIO_HIGH);
    run(SLICE);

    CHECK(count(hi) > 0, "high priority task never ran");
    CHECK_U32(0, count(lo));
}

static void sleep_keeps_time(int model, uint32_t cpu)
{
    uint32_t s, bg, t, laps;

    setup(model, cpu);
    s  = block("kernel:_task_sleep", 5, 0, NULL);
    bg = block(NULL, 0, 0, NULL);
    spawn("sleeper", "body_sleeper", s, 1024, PRIO_HIGH);
    spawn("bg", "body_spinner", bg, 1024, PRIO_NORMAL);
    run(SLICE);

    t = ticks(); laps = count(s);
    CHECK(t > 100, "only %u ticks", t);
    CHECK(laps >= t / 5 - 1 && laps <= t / 5 + 2,
          "%u laps of sleep(5) in %u ticks", laps, t);
    CHECK(count(bg) > 0, "sleeping did not give up the CPU");
}

#define B_RESULT 20

/*
 * A task that returns is gone, and so is everything it held: its stack, its
 * control block. Measured from inside by two sampler tasks, because an idle
 * machine is sitting in STOP and cannot be called into from out here - and
 * idle is exactly when zombies are reaped.
 */
static void exit_is_reaped(int model, uint32_t cpu)
{
    uint32_t pool = ALLOC_FAST, before, b, avail, check;

    setup(model, cpu);
    avail = block("kernel:_task_sleep", 10, ALLOC_FAST, "kernel:_mem_avail");
    check = block("kernel:_task_sleep", 10, 0, "kernel:_mem_check");
    h_poke32(check + B_RESULT, 0xFFFFFFFFu);
    spawn("avail", "body_sampler", avail, 1024, PRIO_LOW);
    spawn("check", "body_sampler", check, 1024, PRIO_LOW);

    before = kcall("kernel:_mem_avail", 1, &pool);
    b = block(NULL, 0, 0, NULL);
    spawn("once", "body_once", b, 2048, PRIO_NORMAL);
    CHECK(kcall("kernel:_mem_avail", 1, &pool) < before - 2048, "no stack allocated?");

    run(60 * TICK);

    CHECK_U32(1, count(b));
    CHECK(count(avail) >= 3, "sampler ran %u times", count(avail));
    CHECK_U32(before, h_peek32(avail + B_RESULT));
    CHECK_U32(0, h_peek32(check + B_RESULT));
}

static void wait_and_wake(int model, uint32_t cpu)
{
    uint8_t zero[8] = {0};
    uint32_t q = h_alloc(zero, sizeof zero);
    uint32_t w, k;

    setup(model, cpu);
    /* The waker sleeps before each wake, and its first sleep is long: the
     * first look at the machine is of a waiter nobody has woken yet. */
    w = block("kernel:_task_wait", q, 0, NULL);
    k = block("kernel:_task_sleep", 40, q, "kernel:_wake_one");
    spawn("waiter", "body_waiter", w, 1024, PRIO_HIGH);
    spawn("waker", "body_waker", k, 1024, PRIO_NORMAL);

    run(30 * TICK);
    CHECK_U32(0, count(w));
    CHECK_U32(0, count(k));

    run(250 * TICK);
    CHECK(count(k) >= 5, "waker ran %u laps", count(k));
    CHECK(count(k) - count(w) <= 1, "woken %u times for %u wakes", count(w), count(k));
}

static void wake_all_wakes_all(int model, uint32_t cpu)
{
    uint8_t zero[8] = {0};
    uint32_t q = h_alloc(zero, sizeof zero);
    uint32_t w[3], k;
    int i;

    setup(model, cpu);
    for (i = 0; i < 3; i++) {
        w[i] = block("kernel:_task_wait", q, 0, NULL);
        spawn("waiter", "body_waiter", w[i], 1024, PRIO_HIGH);
    }
    k = block("kernel:_task_sleep", 3, q, "kernel:_wake_all");
    spawn("waker", "body_waker", k, 1024, PRIO_NORMAL);
    run(SLICE);

    CHECK(count(k) > 20, "waker ran %u laps", count(k));
    for (i = 0; i < 3; i++)
        CHECK(count(k) - count(w[i]) <= 1,
              "waiter %d woken %u times for %u wakes", i, count(w[i]), count(k));
}

/* Two tasks printing at once, preempted every few hundred instructions. A
 * line is one kprintf call and has to come out as one line. */
static void kprintf_lines_stay_whole(int model, uint32_t cpu)
{
    const char *p, *nl;
    uint32_t a, b;
    int lines = 0;

    setup(model, cpu);
    /* Fast, but not so fast that the tick itself eats the machine: at a
     * period near the tick's own cost, only code inside a critical section
     * makes progress and the other task starves. 50Hz is ~141,800 cycles. */
    h_vbl_every(5003);
    a = block("kernel:_kprintf", 3, h_str("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"), NULL);
    b = block("kernel:_kprintf", 3, h_str("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"), NULL);
    spawn("a", "body_caller2", a, 2048, PRIO_NORMAL);
    spawn("b", "body_caller2", b, 2048, PRIO_NORMAL);
    run(SLICE);

    CHECK(count(a) > 3 && count(b) > 3, "a=%u b=%u lines", count(a), count(b));
    for (p = h_serial(); (nl = strchr(p, '\n')) != NULL; p = nl + 1, lines++) {
        const char *c;
        for (c = p; c < nl; c++)
            if (*c != *p && *c != '\r') {
                t_fail("line %d is two tasks' output mixed: %.*s",
                       lines, (int)(nl - p), p);
                return;
            }
    }
    CHECK(lines > 6, "only %d complete lines", lines);
}

/* --- output that outruns the wire ------------------------------------------
 *
 * 9600 baud is about 7400 cycles a character and a task can produce text
 * hundreds of times faster, so the transmit ring is full almost at once and
 * stays full. What the producer does then is the whole question. Polling
 * for room with interrupts masked - what this replaced - holds the CPU at
 * wire speed for the length of a line: lower priority tasks never run,
 * ticks are lost, and received bytes are overrun in Paula's one-byte buffer.
 */
#define FLOOD_LINE "0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"

static uint32_t start_flood(void)
{
    uint32_t b = block("kernel:_kprintf", 3, h_str(FLOOD_LINE), NULL);
    spawn("flood", "body_caller2", b, 2048, PRIO_NORMAL);
    h_serial_set_timing(7400, 14800);
    return b;
}

static void full_ring_sleeps(int model, uint32_t cpu)
{
    uint32_t bg, expect;

    setup(model, cpu);
    start_flood();
    bg = block(NULL, 0, 0, NULL);
    spawn("bg", "body_spinner", bg, 1024, PRIO_LOW);    /* outranked */
    run(SLICE * 4);

    /* bg runs only while the flooder is asleep - which should be nearly
     * always. A lone spinner manages a lap per 30 cycles on a 68000. It
     * will not get all of them: output at 9600 baud is an interrupt per
     * character, about a fifth of the machine, and this test ticks seven
     * times faster than 50Hz. Polling, the answer was zero. */
    expect = (uint32_t)(SLICE * 4 / 30);
    CHECK(count(bg) > expect / 2,
          "low priority task got %u laps of a possible ~%u", count(bg), expect);
}

static void ticks_survive_heavy_output(int model, uint32_t cpu)
{
    uint32_t expect;

    setup(model, cpu);
    start_flood();
    run(SLICE * 4);

    expect = (uint32_t)(SLICE * 4 / TICK);
    CHECK(ticks() >= expect - 2, "%u ticks of %u: the rest were lost to "
          "critical sections longer than a tick", ticks(), expect);
}

static void input_survives_heavy_output(int model, uint32_t cpu)
{
    const char *typed = "the quick brown fox jumps over the lazy dog";

    setup(model, cpu);
    start_flood();
    run(SLICE);                                         /* ring is full now */

    h_serial_rx_pacing(7400);
    h_serial_input(typed);
    run(SLICE * 2);

    CHECK_U32(0, h_peek32(h_sym("kernel:_ser_rx_overruns")));
    CHECK_U32(0, h_peek32(h_sym("kernel:_ser_rx_dropped")));
    CHECK_U32(43, h_peek32(h_sym("kernel:_ser_rx_total")));
}

/*
 * The caller that must NOT sleep: one that arrived with interrupts masked.
 * It is inside a critical section of its own, and switching tasks there
 * hands its half-updated state to whoever runs next. So it polls, as
 * before - seen here as a second task that never gets a look in.
 */
static void masked_writer_never_sleeps(int model, uint32_t cpu)
{
    uint32_t w, other;

    setup(model, cpu);
    h_serial_set_timing(2000, 4000);
    w = block("kernel:_kprintf", 3, h_str(FLOOD_LINE), NULL);
    other = block(NULL, 0, 0, NULL);
    spawn("masked", "body_masked_caller2", w, 2048, PRIO_NORMAL);
    spawn("other", "body_spinner", other, 1024, PRIO_NORMAL);
    run(SLICE * 2);

    CHECK(count(w) > 20, "masked writer made no progress: %u lines", count(w));
    CHECK_U32(0, count(other));
    CHECK_U32(0, h_serial_overruns());
}

/* Blocking must not cost correctness: every line whole, none lost, none
 * reordered, two producers at once. */
static void flood_output_is_intact(int model, uint32_t cpu)
{
    const char *p, *nl;
    uint32_t a, b;
    int lines = 0;

    setup(model, cpu);
    a = block("kernel:_kprintf", 3, h_str("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"), NULL);
    b = block("kernel:_kprintf", 3, h_str("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"), NULL);
    spawn("a", "body_caller2", a, 2048, PRIO_NORMAL);
    spawn("b", "body_caller2", b, 2048, PRIO_NORMAL);
    h_serial_set_timing(2000, 4000);
    run(SLICE * 6);

    CHECK_U32(0, h_serial_overruns());
    for (p = h_serial(); (nl = strchr(p, '\n')) != NULL; p = nl + 1, lines++) {
        size_t len = (size_t)(nl - p);
        const char *c;
        if (len != (*p == 'a' ? 61u : 41u)) {           /* text + \r */
            t_fail("line %d is %u long: %.*s", lines, (unsigned)len, (int)len, p);
            return;
        }
        for (c = p; c < nl - 1; c++)
            if (*c != *p) {
                t_fail("line %d is mixed: %.*s", lines, (int)len, p);
                return;
            }
    }
    CHECK(lines > 100, "only %d lines", lines);
    CHECK(count(a) > 20 && count(b) > 20, "one producer starved: a=%u b=%u",
          count(a), count(b));
}

/*
 * What the whole stack was built for: a task blocked on input costs nothing
 * until a byte arrives, and then runs at once. RBF interrupt -> ring ->
 * wake_one -> isr_exit switches to the reader, ahead of the spinner it
 * outranks.
 */
static void read_blocks_until_input(int model, uint32_t cpu)
{
    uint8_t zero[8] = {0};
    uint32_t buf = h_alloc(zero, sizeof zero);
    uint32_t rd, bg, before;

    setup(model, cpu);
    rd = block("kernel:_ser_read", buf, 1, NULL);       /* ser_read(buf, 1) */
    bg = block(NULL, 0, 0, NULL);
    spawn("reader", "body_caller2", rd, 2048, PRIO_HIGH);
    spawn("bg", "body_spinner", bg, 1024, PRIO_NORMAL);

    run(SLICE / 4);
    CHECK_U32(0, count(rd));                            /* blocked... */
    CHECK(count(bg) > 0, "a blocked reader held the CPU");

    before = count(bg);
    h_serial_rx_pacing(7400);
    h_serial_input("xyz");
    run(SLICE / 4);
    CHECK_U32(3, count(rd));                            /* ...until now */
    CHECK_U32('z', h_peek8(buf));
    CHECK(count(bg) > before, "the reader did not go back to sleep");
}

/* The console: a task on ser0. Type a command, get an answer. */
static void console_answers(int model, uint32_t cpu)
{
    setup(model, cpu);
    kcall("kernel:_console_init", 0, NULL);
    run(SLICE / 4);
    CHECK_CONTAINS("amag> ", h_serial());

    h_serial_clear();
    h_serial_rx_pacing(7400);
    h_serial_input("mem\r");
    run(SLICE);
    CHECK_CONTAINS("mem", h_serial());                  /* echoed */
    CHECK_CONTAINS("fast", h_serial());
    CHECK_CONTAINS("heap check: ok", h_serial());

    h_serial_clear();
    h_serial_input("ps\r");
    run(SLICE);
    CHECK_CONTAINS("console", h_serial());
    CHECK_CONTAINS("idle", h_serial());

    h_serial_clear();
    h_serial_input("frobnicate\r");
    run(SLICE);
    CHECK_CONTAINS("unknown command", h_serial());
    CHECK_CONTAINS("amag> ", h_serial());
}

static void console_enters_debugger(int model, uint32_t cpu)
{
    h_result r;

    setup(model, cpu);
    kcall("kernel:_console_init", 0, NULL);
    run(SLICE / 4);
    h_serial_clear();
    h_serial_rx_pacing(7400);
    h_serial_input("debug\r");
    h_set_cycle_budget(SLICE * 2);
    r = h_resume();
    (void)r;
    CHECK_CONTAINS("AMAG Debugger", h_serial());
}

/* The console watching the keyboard: a task blocked on one device, fed by
 * another, reporting on a third. */
static void console_shows_keys(int model, uint32_t cpu)
{
    setup(model, cpu);
    kcall("kernel:_console_init", 0, NULL);
    run(SLICE / 4);
    h_serial_rx_pacing(7400);

    h_serial_clear();
    h_serial_input("keymap dk\r");
    run(SLICE);
    CHECK_CONTAINS("keymap: dk", h_serial());

    h_serial_clear();
    h_serial_input("keymap klingon\r");
    run(SLICE);
    CHECK_CONTAINS("no keymap 'klingon'", h_serial());
    CHECK_CONTAINS("keymap: dk", h_serial());

    h_serial_clear();
    h_serial_input("keys\r");
    run(SLICE / 2);
    h_key(0x20);                                        /* A down */
    h_key(0x20 | 0x80);
    h_key(0x50);                                        /* F1: no character */
    run(SLICE);
    CHECK_CONTAINS("code $20 down", h_serial());
    CHECK_CONTAINS("'a'", h_serial());
    CHECK_CONTAINS("code $20 up", h_serial());
    CHECK_CONTAINS("code $50 down", h_serial());

    h_serial_clear();
    h_serial_input("x");                                /* any key: stop */
    run(SLICE);
    CHECK_CONTAINS("events dropped", h_serial());
    CHECK_CONTAINS("amag> ", h_serial());
}

/* Backspace edits the line; what reaches the command is what is left. */
static void console_line_editing(int model, uint32_t cpu)
{
    setup(model, cpu);
    kcall("kernel:_console_init", 0, NULL);
    run(SLICE / 4);
    h_serial_clear();
    h_serial_rx_pacing(7400);
    h_serial_input("mex\bm\r");                         /* "mex", oops, "mem" */
    run(SLICE);
    CHECK_CONTAINS("heap check: ok", h_serial());
}

/* No MMU, so nothing stops a task running off the end of its stack except
 * the kernel looking. It looks at every switch. */
static void overflow_is_caught(int model, uint32_t cpu)
{
    uint32_t b;
    h_result r;

    setup(model, cpu);
    b = block("kernel:_task_yield", 200, 0, NULL);      /* 800 bytes onto 512 */
    spawn("greedy", "body_overflow", b, 512, PRIO_NORMAL);

    h_set_cycle_budget(SLICE * 4);
    h_set_sp(IDLE_STACK);
    h_set_sr(0x2000);
    r = h_run(h_sym("kernel:_sched_start"));
    (void)r;

    CHECK_CONTAINS("stack overflow", h_serial());
    CHECK_CONTAINS("greedy", h_serial());
    CHECK_CONTAINS("AMAG Debugger", h_serial());
}

static void stack_high_water(int model, uint32_t cpu)
{
    uint32_t b, t, unused;

    setup(model, cpu);
    b = block("kernel:_task_yield", 0, 0, NULL);
    t = spawn("y", "body_yielder", b, 2048, PRIO_NORMAL);
    unused = kcall("kernel:_task_stack_unused", 1, &t);
    CHECK(unused > 1900 && unused < 2048, "fresh task: %u unused of 2048", unused);

    run(SLICE / 4);
    h_vbl_every(0);                             /* not resumed after this */
    h_set_sr(0x2700);
    {
        uint32_t after = kcall("kernel:_task_stack_unused", 1, &t);
        CHECK(after < unused && after > 1024, "after running: %u unused", after);
    }
}

static void create_fails_cleanly(int model, uint32_t cpu)
{
    uint32_t pool = ALLOC_FAST, before, b;

    setup(model, cpu);
    before = kcall("kernel:_mem_avail", 1, &pool);
    b = block(NULL, 0, 0, NULL);
    CHECK_U32(0, spawn("huge", "body_spinner", b, HEAP_SIZE * 2, PRIO_NORMAL));
    CHECK_U32(0, spawn("noprio", "body_spinner", b, 1024, 9));
    CHECK_U32(before, kcall("kernel:_mem_avail", 1, &pool));
    CHECK_U32(0, kcall("kernel:_mem_check", 0, NULL));
}

/* With nothing to run the machine must be stopped, not spinning. */
static void idle_halts(int model, uint32_t cpu)
{
    uint32_t idle = h_sym("kernel:_cpu_idle");
    int i, stopped = 0;

    setup(model, cpu);
    run(SLICE / 8);
    for (i = 0; i < 50; i++) {
        run(1000);
        if (h_get_pc() >= idle && h_get_pc() <= idle + 6)
            stopped++;
    }
    CHECK(stopped >= 40, "sampled inside cpu_idle %d times in 50", stopped);
    CHECK(ticks() > 0, "no ticks while idle");
}

/* --- registration: everything, on both frame formats ---------------------- */

#define ON(fn) \
    static void fn##_68000(void) { g_started = 0; fn(68000, CPU_68000); } \
    static void fn##_68020(void) { g_started = 0; fn(68020, CPU_68020); }

ON(yield_alternates)
ON(tick_preempts)
ON(tick_cost_is_bounded)
ON(registers_survive)
ON(no_switch_above_task_level)
ON(priority_wins)
ON(sleep_keeps_time)
ON(exit_is_reaped)
ON(wait_and_wake)
ON(wake_all_wakes_all)
ON(kprintf_lines_stay_whole)
ON(full_ring_sleeps)
ON(ticks_survive_heavy_output)
ON(input_survives_heavy_output)
ON(masked_writer_never_sleeps)
ON(flood_output_is_intact)
ON(read_blocks_until_input)
ON(console_answers)
ON(console_shows_keys)
ON(console_line_editing)
ON(console_enters_debugger)
ON(overflow_is_caught)
ON(stack_high_water)
ON(create_fails_cleanly)
ON(idle_halts)

/* The frame is the CPU-specific part, so the test that leans on it hardest
 * also runs on the other two cores there are. */
static void registers_survive_68010(void) { g_started = 0; registers_survive(68010, CPU_68010); }
static void registers_survive_68040(void) { g_started = 0; registers_survive(68040, CPU_68040); }

#define T(name, fn) { name ".68000", fn##_68000, NULL }, { name ".68020", fn##_68020, NULL }

static const test_case tests[] = {
    T("yield_alternates",   yield_alternates),
    T("tick_preempts",      tick_preempts),
    T("tick_cost_bounded",  tick_cost_is_bounded),
    T("registers_survive",  registers_survive),
    { "registers_survive.68010", registers_survive_68010, NULL },
    { "registers_survive.68040", registers_survive_68040, NULL },
    T("no_switch_when_masked", no_switch_above_task_level),
    T("priority_wins",      priority_wins),
    T("sleep_keeps_time",   sleep_keeps_time),
    T("exit_is_reaped",     exit_is_reaped),
    T("wait_and_wake",      wait_and_wake),
    T("wake_all",           wake_all_wakes_all),
    T("kprintf_lines_whole", kprintf_lines_stay_whole),
    T("full_ring_sleeps",   full_ring_sleeps),
    T("ticks_survive_output", ticks_survive_heavy_output),
    T("input_survives_output", input_survives_heavy_output),
    T("masked_writer_polls", masked_writer_never_sleeps),
    T("flood_output_intact", flood_output_is_intact),
    T("read_blocks_until_input", read_blocks_until_input),
    T("console_answers",    console_answers),
    T("console_shows_keys", console_shows_keys),
    T("console_line_editing", console_line_editing),
    T("console_debug_cmd",  console_enters_debugger),
    T("overflow_is_caught", overflow_is_caught),
    T("stack_high_water",   stack_high_water),
    T("create_fails_cleanly", create_fails_cleanly),
    T("idle_halts",         idle_halts),
};

const test_suite task_suite = { "task", tests, sizeof tests / sizeof tests[0] };
