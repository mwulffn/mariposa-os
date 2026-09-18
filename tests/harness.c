#include "harness.h"
#include "m68k.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ memory */

static uint8_t *g_chip;
static uint8_t *g_fast;
static uint8_t *g_rom;

/* ------------------------------------------------------------------ faults */

static int  g_fault_count;
static char g_fault_detail[256];

static void fault(const char *fmt, ...)
{
    va_list ap;
    if (g_fault_count++ == 0) {
        va_start(ap, fmt);
        vsnprintf(g_fault_detail, sizeof g_fault_detail, fmt, ap);
        va_end(ap);
    }
}

/* ------------------------------------------------------- serial (Paula UART) */

#define CUSTOM_BASE 0xDFF000u
#define REG_SERDATR 0x018u
#define REG_SERDAT  0x030u

#define SERDATF_TBE  0x2000u   /* transmit buffer empty  */
#define SERDATF_TSRE 0x1000u   /* transmit shift empty   */
#define SERDATF_RBF  0x4000u   /* receive buffer full    */

static char   g_tx[64 * 1024];
static size_t g_tx_len;

static char   g_rx[4096];
static size_t g_rx_len;
static size_t g_rx_pos;

static uint16_t serdatr_value(void)
{
    /* Transmitter is always ready - nothing here is rate limited. */
    uint16_t v = SERDATF_TBE | SERDATF_TSRE;
    if (g_rx_pos < g_rx_len)
        v |= SERDATF_RBF | (uint8_t)g_rx[g_rx_pos];
    return v;
}

/* ------------------------------------------------------------- guest access */

/* Host pointer for a guest address, or NULL if unmapped. Does not fault -
 * used by both the CPU callbacks (which fault separately) and by the
 * host-side peek/poke helpers, which must not disturb the fault state. */
static uint8_t *raw_ptr(uint32_t addr, int *writable)
{
    if (addr < H_CHIP_BASE + H_CHIP_SIZE) {
        if (writable) *writable = 1;
        return g_chip + (addr - H_CHIP_BASE);
    }
    if (addr >= H_FAST_BASE && addr < H_FAST_BASE + H_FAST_SIZE) {
        if (writable) *writable = 1;
        return g_fast + (addr - H_FAST_BASE);
    }
    if (addr >= H_ROM_BASE && addr < H_ROM_BASE + H_ROM_SIZE) {
        if (writable) *writable = 0;
        return g_rom + (addr - H_ROM_BASE);
    }
    return NULL;
}

static uint32_t raw_read(const uint8_t *p, int size)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < size; i++)
        v = (v << 8) | p[i];
    return v;
}

static void raw_write(uint8_t *p, int size, uint32_t v)
{
    int i;
    for (i = size - 1; i >= 0; i--) {
        p[i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

/* ------------------------------------------------------- hardware registers */

/* Regions we model well enough not to hang, but do not implement. Reads
 * return a value that means "nothing here"; writes are discarded. */
static int stub_region(uint32_t addr, uint32_t *read_value)
{
    /* CIA-A and CIA-B. Reset code writes control and interrupt registers. */
    if (addr >= 0xBFD000u && addr <= 0xBFEF01u) { *read_value = 0x00; return 1; }

    /* Zorro II autoconfig space: $FF from register 0 means "no card". */
    if (addr >= 0xE80000u && addr < 0xE90000u) { *read_value = 0xFF; return 1; }

    /* Gayle IDE. $7F is the status the ROM reads as "no drive", which makes
     * ide_test_read bail cleanly. Replace this with a disk model to test
     * ide.s / partition.s / filesystem.s headlessly. */
    if (addr >= 0xDA0000u && addr < 0xDA2000u) { *read_value = 0x7F; return 1; }

    return 0;
}

static uint32_t custom_read(uint32_t addr, int size)
{
    uint32_t off = addr - CUSTOM_BASE;

    /* SERDATR. A word read is what serial_get_char uses to take the
     * character, so that is where the receive buffer is consumed. The byte
     * reads are serial_put_char's and serial_get_char's btst polling the
     * high half - those must not consume anything. */
    if (off == REG_SERDATR && size == 2) {
        uint16_t v = serdatr_value();
        if (g_rx_pos < g_rx_len)
            g_rx_pos++;
        return v;
    }
    if (off == REG_SERDATR && size == 1) return serdatr_value() >> 8;
    if (off == REG_SERDATR + 1 && size == 1) return serdatr_value() & 0xFF;

    /* Everything else in custom space reads as zero. DMACONR/INTENAR/VPOSR
     * would go here if a test needed them. */
    return 0;
}

static void custom_write(uint32_t addr, int size, uint32_t val)
{
    uint32_t off = addr - CUSTOM_BASE;
    (void)size;

    if (off == REG_SERDAT) {
        if (g_tx_len + 1 < sizeof g_tx)
            g_tx[g_tx_len++] = (char)(val & 0xFF);
        return;
    }
    /* SERPER, COLOR00, DMACON, INTENA, bitplane and copper registers: the
     * routines under test write these freely and nothing reads them back. */
}

/* ------------------------------------------------------- Musashi callbacks */

static uint32_t cpu_read(uint32_t addr, int size)
{
    uint8_t *p;
    uint32_t stub;

    addr &= 0xFFFFFFu;                     /* 68000 has a 24-bit address bus */

    if (size > 1 && (addr & 1)) {
        fault("address error: %d-bit read at odd address $%06X (pc $%06X)",
              size * 8, addr, m68k_get_reg(NULL, M68K_REG_PPC));
        return 0;
    }
    if (addr >= CUSTOM_BASE && addr < CUSTOM_BASE + 0x200u)
        return custom_read(addr, size);
    if (stub_region(addr, &stub))
        return stub;
    if ((p = raw_ptr(addr, NULL)) != NULL)
        return raw_read(p, size);

    fault("read of unmapped memory at $%06X (pc $%06X)",
          addr, m68k_get_reg(NULL, M68K_REG_PPC));
    return 0;
}

static void cpu_write(uint32_t addr, int size, uint32_t val)
{
    uint8_t *p;
    uint32_t stub;
    int writable = 0;

    addr &= 0xFFFFFFu;

    if (size > 1 && (addr & 1)) {
        fault("address error: %d-bit write at odd address $%06X (pc $%06X)",
              size * 8, addr, m68k_get_reg(NULL, M68K_REG_PPC));
        return;
    }
    if (addr >= CUSTOM_BASE && addr < CUSTOM_BASE + 0x200u) {
        custom_write(addr, size, val);
        return;
    }
    if (stub_region(addr, &stub))
        return;
    if ((p = raw_ptr(addr, &writable)) != NULL) {
        if (!writable) {
            fault("write to ROM at $%06X (pc $%06X)",
                  addr, m68k_get_reg(NULL, M68K_REG_PPC));
            return;
        }
        raw_write(p, size, val);
        return;
    }

    fault("write to unmapped memory at $%06X (pc $%06X)",
          addr, m68k_get_reg(NULL, M68K_REG_PPC));
}

unsigned int m68k_read_memory_8 (unsigned int a) { return cpu_read(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return cpu_read(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return cpu_read(a, 4); }

void m68k_write_memory_8 (unsigned int a, unsigned int v) { cpu_write(a, 1, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { cpu_write(a, 2, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { cpu_write(a, 4, v); }

/* The disassembler reads through these. They must not record faults - the
 * disassembler is a host-side debugging aid, not guest execution. */
unsigned int m68k_read_disassembler_8 (unsigned int a)
{ uint8_t *p = raw_ptr(a & 0xFFFFFFu, NULL); return p ? raw_read(p, 1) : 0; }
unsigned int m68k_read_disassembler_16(unsigned int a)
{ uint8_t *p = raw_ptr(a & 0xFFFFFFu, NULL); return p ? raw_read(p, 2) : 0; }
unsigned int m68k_read_disassembler_32(unsigned int a)
{ uint8_t *p = raw_ptr(a & 0xFFFFFFu, NULL); return p ? raw_read(p, 4) : 0; }

/* ------------------------------------------------------------- symbol table */

typedef struct { char name[64]; uint32_t addr; } sym_t;

static sym_t  *g_syms;
static size_t  g_nsyms;

static void load_symbols(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    size_t cap = 0;

    if (!f) {
        fprintf(stderr,
            "harness: cannot open symbol file '%s'\n"
            "         build the ROM first: make -C src/rom\n", path);
        exit(2);
    }
    while (fgets(line, sizeof line, f)) {
        char name[64];
        unsigned long addr;
        if (sscanf(line, "%63s %lx", name, &addr) != 2)
            continue;
        if (g_nsyms == cap) {
            cap = cap ? cap * 2 : 256;
            g_syms = realloc(g_syms, cap * sizeof *g_syms);
            if (!g_syms) { fprintf(stderr, "harness: out of memory\n"); exit(2); }
        }
        snprintf(g_syms[g_nsyms].name, sizeof g_syms[g_nsyms].name, "%s", name);
        g_syms[g_nsyms].addr = (uint32_t)addr;
        g_nsyms++;
    }
    fclose(f);

    if (g_nsyms == 0) {
        fprintf(stderr,
            "harness: symbol file '%s' is empty\n"
            "         tests/mksym.py may not understand this vasm's listing format\n",
            path);
        exit(2);
    }
}

uint32_t h_sym(const char *name)
{
    size_t i;
    for (i = 0; i < g_nsyms; i++)
        if (strcmp(g_syms[i].name, name) == 0)
            return g_syms[i].addr;

    fprintf(stderr,
        "harness: no ROM symbol named '%s'\n"
        "         it may have been renamed, or it is a local label (.foo),\n"
        "         which vasm does not export\n", name);
    exit(2);
}

/* ------------------------------------------------------------------ scratch */

static uint32_t g_scratch_next;

uint32_t h_alloc(const void *data, size_t len)
{
    uint32_t addr = g_scratch_next;
    uint8_t *p;

    if (len > H_SCRATCH_SIZE ||
        addr + len > H_SCRATCH_BASE + H_SCRATCH_SIZE) {
        fprintf(stderr, "harness: scratch area exhausted\n");
        exit(2);
    }
    p = raw_ptr(addr, NULL);
    if (data) memcpy(p, data, len);
    else      memset(p, 0, len);

    g_scratch_next = (uint32_t)((addr + len + 3) & ~3u);   /* keep it even */
    return addr;
}

uint32_t h_str(const char *s) { return h_alloc(s, strlen(s) + 1); }

/* --------------------------------------------------------------- peek/poke */

uint8_t  h_peek8 (uint32_t a) { uint8_t *p = raw_ptr(a, NULL); return p ? (uint8_t)raw_read(p, 1) : 0; }
uint16_t h_peek16(uint32_t a) { uint8_t *p = raw_ptr(a, NULL); return p ? (uint16_t)raw_read(p, 2) : 0; }
uint32_t h_peek32(uint32_t a) { uint8_t *p = raw_ptr(a, NULL); return p ? raw_read(p, 4) : 0; }

void h_poke8 (uint32_t a, uint8_t  v) { uint8_t *p = raw_ptr(a, NULL); if (p) raw_write(p, 1, v); }
void h_poke16(uint32_t a, uint16_t v) { uint8_t *p = raw_ptr(a, NULL); if (p) raw_write(p, 2, v); }
void h_poke32(uint32_t a, uint32_t v) { uint8_t *p = raw_ptr(a, NULL); if (p) raw_write(p, 4, v); }

void h_peekstr(uint32_t addr, char *buf, size_t bufsz)
{
    size_t i;
    for (i = 0; i + 1 < bufsz; i++) {
        uint8_t c = h_peek8(addr + (uint32_t)i);
        if (!c) break;
        buf[i] = (char)c;
    }
    buf[i] = '\0';
}

/* -------------------------------------------------------------- registers */

void h_set_d(int n, uint32_t v) { m68k_set_reg((m68k_register_t)(M68K_REG_D0 + n), v); }
void h_set_a(int n, uint32_t v) { m68k_set_reg((m68k_register_t)(M68K_REG_A0 + n), v); }
uint32_t h_get_d(int n) { return m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + n)); }
uint32_t h_get_a(int n) { return m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + n)); }

void     h_set_sp(uint32_t v) { m68k_set_reg(M68K_REG_SP, v); }
uint32_t h_get_sp(void)       { return m68k_get_reg(NULL, M68K_REG_SP); }

/* ------------------------------------------------------------- run control */

static uint64_t g_budget = 20000000;

void h_set_cycle_budget(uint64_t c) { g_budget = c; }

static const char *vector_name(int v)
{
    switch (v) {
        case 2:  return "BUS ERROR";
        case 3:  return "ADDRESS ERROR";
        case 4:  return "ILLEGAL INSTRUCTION";
        case 5:  return "DIVIDE BY ZERO";
        case 6:  return "CHK";
        case 7:  return "TRAPV";
        case 8:  return "PRIVILEGE VIOLATION";
        case 9:  return "TRACE";
        case 10: return "LINE-A";
        case 11: return "LINE-F";
        case 24: return "SPURIOUS";
        default: break;
    }
    if (v >= 25 && v <= 31) return "AUTOVECTOR";
    if (v >= 32 && v <= 47) return "TRAP";
    return "UNKNOWN";
}

void h_begin_call(void) { m68k_set_reg(M68K_REG_SP, H_STACK_TOP); }

void h_push32(uint32_t v)
{
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP) - 4;
    m68k_set_reg(M68K_REG_SP, sp);
    h_poke32(sp, v);
}

h_result h_call(uint32_t pc)
{
    h_push32(H_RETURN_ADDR);
    return h_run(pc);
}

h_result h_run(uint32_t pc)
{
    h_result r;
    memset(&r, 0, sizeof r);

    m68k_set_reg(M68K_REG_PC, pc);

    for (;;) {
        uint32_t cur = m68k_get_reg(NULL, M68K_REG_PC);

        if (cur == H_RETURN_ADDR) {
            r.status = H_OK;
            break;
        }
        if (cur >= H_VECTOR_TRAP && cur < H_VECTOR_TRAP + 64 * 4) {
            r.status = H_EXCEPTION;
            r.vector = (int)((cur - H_VECTOR_TRAP) / 4);
            snprintf(r.detail, sizeof r.detail, "%s (vector %d)",
                     vector_name(r.vector), r.vector);
            break;
        }
        if (g_fault_count) {
            r.status = H_FAULT;
            snprintf(r.detail, sizeof r.detail, "%s", g_fault_detail);
            break;
        }
        if (r.cycles >= g_budget) {
            r.status = H_TIMEOUT;
            snprintf(r.detail, sizeof r.detail,
                     "no return after %llu cycles, pc $%06X",
                     (unsigned long long)r.cycles, cur);
            break;
        }
        /* One instruction at a time: the run loop has to see PC land on a
         * sentinel, and a multi-instruction slice could step straight past it. */
        r.cycles += (uint64_t)m68k_execute(1);
    }

    /* A fault during an otherwise clean return still fails the call. */
    if (r.status == H_OK && g_fault_count) {
        r.status = H_FAULT;
        snprintf(r.detail, sizeof r.detail, "%s", g_fault_detail);
    }
    return r;
}

/* ---------------------------------------------------------------- lifecycle */

const char *h_serial(void)   { g_tx[g_tx_len] = '\0'; return g_tx; }
size_t      h_serial_len(void) { return g_tx_len; }

void h_serial_input(const char *s)
{
    size_t n = strlen(s);
    if (n > sizeof g_rx) n = sizeof g_rx;
    memcpy(g_rx, s, n);
    g_rx_len = n;
    g_rx_pos = 0;
}

void h_reset(void)
{
    int v;

    memset(g_chip, 0, H_CHIP_SIZE);
    memset(g_fast, 0, H_FAST_SIZE);

    g_tx_len = 0;
    g_rx_len = g_rx_pos = 0;
    g_fault_count = 0;
    g_fault_detail[0] = '\0';
    g_scratch_next = H_SCRATCH_BASE;

    /* Reset reads SSP from $0 and PC from $4. Give it something sane so the
     * reset itself does not look like a wild fetch. */
    h_poke32(0, H_STACK_TOP);
    h_poke32(4, H_ROM_BASE);

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    /* Point every exception vector at its own sentinel, so an exception is
     * identified by where PC lands - no frame decoding needed. A test that
     * calls install_exception_vectors overwrites these, by design. */
    for (v = 2; v < 64; v++)
        h_poke32((uint32_t)v * 4, H_VECTOR_TRAP + (uint32_t)v * 4);

    /* pulse_reset leaves SR at $2700: supervisor, interrupts masked, which
     * is the state ROM code runs in. */
    g_fault_count = 0;
    g_fault_detail[0] = '\0';
}

int h_init(const char *rom_path, const char *sym_path)
{
    FILE *f;
    size_t got;

    g_chip = calloc(1, H_CHIP_SIZE);
    g_fast = calloc(1, H_FAST_SIZE);
    g_rom  = calloc(1, H_ROM_SIZE);
    if (!g_chip || !g_fast || !g_rom) {
        fprintf(stderr, "harness: out of memory\n");
        return 2;
    }

    f = fopen(rom_path, "rb");
    if (!f) {
        fprintf(stderr,
            "harness: cannot open ROM image '%s'\n"
            "         build it first: make -C src/rom\n", rom_path);
        return 2;
    }
    got = fread(g_rom, 1, H_ROM_SIZE, f);
    fclose(f);
    if (got != H_ROM_SIZE) {
        fprintf(stderr,
            "harness: ROM image '%s' is %zu bytes, expected %u\n",
            rom_path, got, (unsigned)H_ROM_SIZE);
        return 2;
    }

    load_symbols(sym_path);
    h_reset();
    return 0;
}

void h_shutdown(void)
{
    free(g_chip); free(g_fast); free(g_rom); free(g_syms);
    g_chip = g_fast = g_rom = NULL; g_syms = NULL; g_nsyms = 0;
}
