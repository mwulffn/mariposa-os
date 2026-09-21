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
#include "vfs.h"
#include "blk.h"
#include "bcache.h"
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

static const char *vfs_error(long rc)
{
    static const char *const text[] = {
        "ok", "no such file, directory or volume", "I/O error", "bad handle",
        "not a directory", "is a directory", "too many open", "no such device, "
        "or no filesystem on it", "busy", "invalid argument", "read-only filesystem",
        "that name is taken", "no space left", "directory not empty"
    };

    return (rc <= 0 && rc >= -13) ? text[-rc] : "error";
}

static void cmd_mount(struct device *con, const char *arg)
{
    const char *volume, *devname, *fstype;
    unsigned long i;
    struct device *d;

    (void)arg;
    for (i = 0; vfs_mount_info(i, &volume, &devname, &fstype); i++)
        say(con, "%s:  on %s  (%s)\n", volume, devname, fstype);
    if (i == 0)
        say(con, "nothing is mounted\n");

    for (d = dev_next(0); d; d = dev_next(d)) {
        const struct blkdev *bd = d->hw;
        const struct blk_partition *p;

        if (d->class != DEV_BLOCK)
            continue;
        p = blk_partition_of(bd);
        say(con, "  %-8s %8lu blocks  %s\n", d->name, bd->blocks, p ? p->label : "(whole disk)");
    }
}

static void cmd_ls(struct device *con, const char *arg)
{
    struct vfs_dirent e;
    int h = vfs_opendir(*arg ? arg : "boot:");
    int rc;

    if (h < 0) {
        say(con, "ls: %s\n", vfs_error(h));
        return;
    }
    while ((rc = vfs_readdir(h, &e)) == 1) {
        if (e.type == VFS_DIR)
            say(con, "%10s  %s/\n", "(dir)", e.name);
        else
            say(con, "%10lu  %s\n", e.size, e.name);
    }
    if (rc < 0)
        say(con, "ls: %s\n", vfs_error(rc));
    vfs_close(h);
}

/* Text as text, anything else as a dot: this is a console, not a pager. */
static void cmd_cat(struct device *con, const char *arg)
{
    char buf[128];
    long n, i;
    int h;

    if (!*arg) {
        say(con, "cat: which file?\n");
        return;
    }
    h = vfs_open(arg);
    if (h < 0) {
        say(con, "cat: %s\n", vfs_error(h));
        return;
    }
    while ((n = vfs_read(h, buf, sizeof buf)) > 0) {
        for (i = 0; i < n; i++)
            if ((buf[i] < ' ' && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t') ||
                buf[i] == 0x7F)
                buf[i] = '.';
        chr_write(con, buf, (unsigned long)n);
    }
    if (n < 0)
        say(con, "cat: %s\n", vfs_error(n));
    vfs_close(h);
}

/* write <path> <text...>: make the file hold that one line. Enough to prove
 * a filesystem takes writes, which is all a console owes anyone. */
static void cmd_write(struct device *con, const char *arg)
{
    char path[128];
    unsigned long n = 0;
    long rc;
    int h;

    while (*arg && *arg != ' ' && n < sizeof path - 1)
        path[n++] = *arg++;
    path[n] = '\0';
    while (*arg == ' ')
        arg++;
    if (!path[0] || !*arg) {
        say(con, "write: write <path> <text>\n");
        return;
    }
    h = vfs_open_flags(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (h < 0) {
        say(con, "write: %s\n", vfs_error(h));
        return;
    }
    rc = vfs_write(h, arg, str_len(arg));
    if (rc >= 0)
        rc = vfs_write(h, "\n", 1);
    if (rc < 0)
        say(con, "write: %s\n", vfs_error(rc));
    vfs_close(h);
}

static void cmd_mkdir(struct device *con, const char *arg)
{
    int rc = *arg ? vfs_mkdir(arg) : VFS_EINVAL;

    if (rc != VFS_OK)
        say(con, "mkdir: %s\n", vfs_error(rc));
}

static void cmd_rm(struct device *con, const char *arg)
{
    int rc = *arg ? vfs_remove(arg) : VFS_EINVAL;

    if (rc != VFS_OK)
        say(con, "rm: %s\n", vfs_error(rc));
}

/*
 * bench <volume:> - what DiskSpeed measures: a big file written and read
 * back in big transfers, timed by the vertical blank. The file is removed
 * afterwards. The buffer is allocated, so it is at an even address and the
 * transfers take the direct path - which is the point.
 */
#define BENCH_CHUNK  32768UL
#define BENCH_CHUNKS 32UL               /* 1MB */

static unsigned long kb_per_second(unsigned long kb, unsigned long ticks)
{
    return ticks ? kb * 50 / ticks : 0;
}

static void cmd_bench(struct device *con, const char *arg)
{
    char path[48];
    unsigned long i, n = 0, t0, write_ticks, read_ticks, bad = 0;
    unsigned long *buf;
    long rc = 0;
    int h;

    while (arg[n] && arg[n] != ' ' && n < sizeof path - 16) {
        path[n] = arg[n];
        n++;
    }
    if (!n || path[n - 1] != ':') {
        say(con, "bench: which volume? bench boot:\n");
        return;
    }
    for (i = 0; "bench.tmp"[i]; i++)
        path[n++] = "bench.tmp"[i];
    path[n] = '\0';

    buf = mem_alloc(BENCH_CHUNK, ALLOC_ANY);
    if (!buf) {
        say(con, "bench: no memory\n");
        return;
    }
    for (i = 0; i < BENCH_CHUNK / 4; i++)
        buf[i] = i * 2654435761UL;

    h = vfs_open_flags(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (h < 0) {
        say(con, "bench: %s\n", vfs_error(h));
        mem_free(buf);
        return;
    }
    say(con, "writing %luKB to %s...\n", BENCH_CHUNK * BENCH_CHUNKS / 1024, path);
    t0 = sched_ticks();
    for (i = 0; i < BENCH_CHUNKS && rc >= 0; i++)
        rc = vfs_write(h, buf, BENCH_CHUNK);
    vfs_close(h);                               /* the sync is part of the cost */
    write_ticks = sched_ticks() - t0;
    if (rc < 0) {
        say(con, "bench: write: %s\n", vfs_error(rc));
    } else {
        h = vfs_open(path);
        t0 = sched_ticks();
        for (i = 0; i < BENCH_CHUNKS && h >= 0; i++) {
            buf[0] = buf[1000] = 0;
            rc = vfs_read(h, buf, BENCH_CHUNK);
            if (rc != (long)BENCH_CHUNK || buf[0] != 0 || buf[1000] != 1000 * 2654435761UL)
                bad++;
        }
        read_ticks = sched_ticks() - t0;
        if (h >= 0)
            vfs_close(h);

        say(con, "write %4lu KB/s   (%lu ticks)\n",
            kb_per_second(BENCH_CHUNK * BENCH_CHUNKS / 1024, write_ticks), write_ticks);
        say(con, "read  %4lu KB/s   (%lu ticks)%s\n",
            kb_per_second(BENCH_CHUNK * BENCH_CHUNKS / 1024, read_ticks), read_ticks,
            bad ? "   DATA DID NOT READ BACK RIGHT" : "");
    }
    vfs_remove(path);
    mem_free(buf);
}

static void cmd_bcache(struct device *con, const char *arg)
{
    unsigned long total = bc_hits + bc_misses;

    (void)arg;
    if (!bc_blocks) {
        say(con, "block cache: off - no memory for it\n");
        return;
    }
    say(con, "block cache: %lu blocks (%luKB), %lu hits, %lu misses",
        bc_blocks, bc_blocks / 2, bc_hits, bc_misses);
    if (total)
        say(con, " (%lu%% from memory)", bc_hits * 100 / total);
    say(con, "\n");
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
    { "mount", cmd_mount, "volumes and block devices" },
    { "ls",   cmd_ls,   "list a directory: ls boot:docs" },
    { "cat",  cmd_cat,  "print a file: cat boot:readme.txt" },
    { "write", cmd_write, "put a line of text in a file: write sys:note.txt hello" },
    { "mkdir", cmd_mkdir, "make a directory" },
    { "rm",   cmd_rm,   "remove a file or an empty directory" },
    { "bench", cmd_bench, "disk speed, DiskSpeed style: bench boot:" },
    { "bcache", cmd_bcache, "block cache hits and misses" },
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
