/*
 * console.c - a command line on a character device
 *
 * A way to ask a running kernel questions without crashing it into the ROM
 * debugger first. It is an ordinary task blocked in chr_read(), so it costs
 * nothing until somebody types, and it talks to a chardev and not to
 * serial.c, so the same code will sit on a screen console when there is one.
 *
 * Everything a console prints - prompt, echo, and what its commands answer -
 * goes to its own device and nowhere else. kprintf is the kernel log and
 * appears on every console; a command's answer is not the kernel log.
 */
#include "console.h"
#include "chardev.h"
#include "kprintf.h"
#include "stdarg.h"
#include "task.h"
#include "mem.h"
#include "irq.h"
#include "serial.h"
#include "cpu.h"
#include "input.h"
#include "kbd.h"
#include "kstring.h"

#define LINE_MAX 80
#define PROMPT   "amag> "

/*
 * Print to one console. Commands use this and never kprintf: kprintf is the
 * kernel log and goes to every console there is, so an answer printed with
 * it turned up on the screen after somebody else's waiting prompt when the
 * question had been asked on the serial line. An answer belongs to whoever
 * asked.
 */
static void say(struct device *con, const char *fmt, ...)
{
    char buf[160], *p, *start;
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    /* \n becomes \r\n: a serial terminal needs both, the screen ignores
     * the extra one. Written in runs, not a character at a time. */
    for (start = p = buf; ; p++) {
        if (*p == '\n' || *p == '\0') {
            if (p > start)
                chr_write(con, start, (unsigned long)(p - start));
            if (*p == '\0')
                break;
            chr_write(con, "\r\n", 2);
            start = p + 1;
        }
    }
}

/* ------------------------------------------------------------- commands --- */

static void cmd_mem(struct device *con, const char *arg)
{
    (void)arg;
    unsigned long rc;

    say(con, "fast  %8lu free, largest %8lu\n",
            mem_avail(ALLOC_FAST), mem_largest(ALLOC_FAST));
    say(con, "slow  %8lu free, largest %8lu\n",
            mem_avail(ALLOC_SLOW), mem_largest(ALLOC_SLOW));
    say(con, "chip  %8lu free, largest %8lu\n",
            mem_avail(ALLOC_CHIP), mem_largest(ALLOC_CHIP));
    rc = mem_check();
    if (rc == MEMCHK_OK)
        say(con, "heap check: ok\n");
    else
        say(con, "heap check: FAILED, code %lu\n", rc);
}

static void cmd_ps(struct device *con, const char *arg)
{
    (void)arg;
    static const char *const state[] = {
        "ready", "running", "sleeping", "waiting", "dead"
    };
    struct task *t;

    say(con, "%-10s %4s  %-8s  %s\n", "task", "prio", "state", "stack unused");
    CRITICAL_ENTER();
    for (t = task_next(0); t; t = task_next(t)) {
        if (t->stack_base)
            say(con, "%-10s %4u  %-8s  %lu of %lu\n", t->name,
                    (unsigned)t->prio, state[t->state],
                    task_stack_unused(t), t->stack_size);
        else
            say(con, "%-10s %4u  %-8s  (boot stack)\n", t->name,
                    (unsigned)t->prio, state[t->state]);
    }
    CRITICAL_EXIT();
}

static void cmd_dev(struct device *con, const char *arg)
{
    (void)arg;
    static const char *const class[] = { "?", "char", "block", "input" };
    struct device *d;

    for (d = dev_next(0); d; d = dev_next(d))
        say(con, "%-8s %s\n", d->name, class[d->class <= DEV_INPUT ? d->class : 0]);
}

static void cmd_irq(struct device *con, const char *arg)
{
    (void)arg;
    say(con, "ticks %lu (%lu s), spurious interrupts %lu\n",
            sched_ticks(), sched_ticks() / 50, irq_spurious);
    say(con, "serial rx: %lu dropped (ring full), %lu overrun (handler late)\n",
            ser_rx_dropped, ser_rx_overruns);
}

/* Show key events as they happen, until a byte arrives on the console - not
 * until a key, because the point is to find out whether keys work. Polls
 * both, at tick rate: there is no way yet to block on two things at once. */
static void cmd_keys(struct device *con, const char *arg)
{
    static const char *const what[] = { "up", "down", "repeat" };
    struct input_event ev;
    char c;

    (void)arg;
    say(con, "keymap %s - press keys on the Amiga; any key here to stop\n",
            input_keymap_name());
    while (!chr_rx_ready(con)) {
        if (!input_pending()) {
            task_sleep(1);
            continue;
        }
        input_read(&ev);
        if (ev.ch > ' ' && ev.ch < 0x7F)
            say(con, "code $%02x %-6s qual $%04x  U+%04x '%c'\n", ev.code,
                    what[ev.value], ev.qual, ev.ch, (int)ev.ch);
        else
            say(con, "code $%02x %-6s qual $%04x  U+%04x\n", ev.code,
                    what[ev.value], ev.qual, ev.ch);
    }
    chr_read(con, &c, 1);
    say(con, "%lu events dropped, %lu protocol codes, %lu reset warnings\n",
            input_dropped, kbd_protocol_codes, kbd_reset_warnings);
}

static void cmd_keymap(struct device *con, const char *arg)
{
    if (*arg && input_set_keymap(arg) != 0)
        say(con, "no keymap '%s' - there is us and dk\n", arg);
    say(con, "keymap: %s\n", input_keymap_name());
}

extern void (*rom_panic)(void);

/* Into the ROM debugger, on purpose, from a kernel that is fine. The flush
 * first, because the ROM bangs the UART and knows nothing of the ring. */
static void cmd_debug(struct device *con, const char *arg)
{
    (void)arg;
    say(con, "entering the ROM debugger\n");
    ser_flush();
    cpu_int_disable();
    rom_panic();
}

static void cmd_help(struct device *con, const char *arg);

static const struct {
    const char *name;
    void (*run)(struct device *con, const char *arg);
    const char *help;
} commands[] = {
    { "help", cmd_help, "this list" },
    { "mem",  cmd_mem,  "free memory per pool, and a heap check" },
    { "ps",   cmd_ps,   "tasks, their state and stack headroom" },
    { "dev",  cmd_dev,  "registered devices" },
    { "irq",  cmd_irq,  "uptime and interrupt counters" },
    { "keys", cmd_keys, "show keyboard events until a key is pressed here" },
    { "keymap", cmd_keymap, "show the keyboard layout, or set it: keymap dk" },
    { "debug", cmd_debug, "drop into the ROM debugger (r, m, ? there)" },
};

#define NCOMMANDS (sizeof commands / sizeof commands[0])

static void cmd_help(struct device *con, const char *arg)
{
    (void)arg;
    unsigned int i;

    for (i = 0; i < NCOMMANDS; i++)
        say(con, "%-7s %s\n", commands[i].name, commands[i].help);
}

static void run_line(struct device *con, char *line)
{
    char *arg = line;
    unsigned int i;

    if (!*line)
        return;

    /* "command argument": cut at the first space. */
    while (*arg && *arg != ' ')
        arg++;
    while (*arg == ' ')
        *arg++ = '\0';

    for (i = 0; i < NCOMMANDS; i++)
        if (str_eq(commands[i].name, line)) {
            commands[i].run(con, arg);
            return;
        }
    say(con, "unknown command '%s' - try help\n", line);
}

/* ----------------------------------------------------------------- task --- */

static void console_task(void *arg)
{
    struct device *con = arg;
    char line[LINE_MAX];
    unsigned int n = 0;
    char c;

    chr_write(con, PROMPT, sizeof PROMPT - 1);
    for (;;) {
        chr_read(con, &c, 1);

        if (c == '\r' || c == '\n') {
            chr_write(con, "\r\n", 2);
            line[n] = '\0';
            run_line(con, line);
            n = 0;
            chr_write(con, PROMPT, sizeof PROMPT - 1);
        } else if (c == '\b' || c == 0x7F) {
            if (n) {
                n--;
                chr_write(con, "\b \b", 3);
            }
        } else if (c >= ' ' && n < LINE_MAX - 1) {
            line[n++] = c;
            chr_write(con, &c, 1);          /* echo */
        }
    }
}

/* One console per character device that exists: the serial line, and the
 * keyboard and screen. They share everything but the line being typed, and
 * what a command prints goes to both, because kprintf does. */
static int start_on(const char *devname, const char *taskname)
{
    struct device *dev = dev_find(devname);

    if (!dev || dev->class != DEV_CHAR)
        return -1;
    return task_create(taskname, console_task, dev, 4096, TASK_PRIO_HIGH) ? 0 : -1;
}

int console_init(void)
{
    int serial = start_on("ser0", "console");

    start_on("con0", "console-kbd");        /* absent without a screen */
    return serial;
}
