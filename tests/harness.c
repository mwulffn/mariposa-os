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

/* Trapdoor RAM at $C00000. Absent unless a test attaches it, so every
 * existing test sees the map it always saw. g_slow_decode is what the board
 * answers to: equal to g_slow_size for a fully decoded board, larger when
 * it mirrors itself up the region. */
static uint8_t *g_slow;
static uint32_t g_slow_size;
static uint32_t g_slow_decode;

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

/* Paula's interrupt registers, modelled further down - the UART needs them:
 * RBF and TBE are interrupt bits before they are status bits. */
static uint16_t g_intreq;
static void irq_update(void);

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
static int    g_rx_ovrun;        /* SERDATR OVRUN; see receiver pacing below */

/* --- transmitter timing --------------------------------------------------
 *
 * The transmitter is not instant. It used to be, which meant no polling loop
 * ever spun, TBE and TSRE were indistinguishable, and the TBE interrupt could
 * never fire - so the interrupt-driven transmit path in docs/serial_design.md
 * was untestable by construction.
 *
 * The delays are deliberately much shorter than 9600 baud (~7400 cycles a
 * character), which would make the disk tests crawl for no extra coverage.
 * They only have to be non-zero, and TSRE has to trail TBE.
 */
static uint64_t g_cycles;          /* free-running, survives across h_call */
static uint64_t g_tbe_at;          /* cycle the buffer frees */
static uint64_t g_tsre_at;         /* cycle the shifter empties */
static uint64_t g_tbe_cycles  = 64;
static uint64_t g_tsre_cycles = 128;
static int      g_tbe_signalled;   /* INTREQ TBE already raised for this byte */
static unsigned g_tx_overruns;     /* SERDAT written while TBE was clear */

static uint16_t serdatr_value(void)
{
    uint16_t v = 0;

    if (g_cycles >= g_tbe_at)  v |= SERDATF_TBE;
    if (g_cycles >= g_tsre_at) v |= SERDATF_TSRE;

    /* RBF mirrors INTREQ bit 11 and is cleared by writing INTREQ, never by
     * reading SERDATR - see the receive note in harness.h. */
    if ((g_intreq & H_INTF_RBF) && g_rx_pos < g_rx_len)
        v |= SERDATF_RBF | (uint8_t)g_rx[g_rx_pos];
    if (g_rx_ovrun)
        v |= 0x8000u;                       /* OVRUN, cleared with RBF */
    return v;
}

/* --- receiver pacing -----------------------------------------------------
 *
 * Unpaced (the default), the next byte latches the moment software
 * acknowledges the last: a sender that waits for the receiver, which no
 * real one does. Paced, a byte arrives every g_rx_pace cycles whether or
 * not anyone is ready, and one that arrives while the last is still
 * unacknowledged is lost and sets OVRUN - which is what a receive handler
 * kept waiting by a long critical section actually faces. */
static uint64_t g_rx_pace;
static uint64_t g_rx_next_at;
static size_t   g_rx_arrive;       /* paced: index of the next byte due */

/* Paula latches the next byte as soon as software acknowledges the last. */
static void serial_rx_latch(void)
{
    if (g_rx_pace)
        return;
    if (!(g_intreq & H_INTF_RBF) && g_rx_pos < g_rx_len)
        g_intreq |= H_INTF_RBF;
}

static void serial_rx_tick(void)
{
    if (!g_rx_pace || g_rx_arrive >= g_rx_len || g_cycles < g_rx_next_at)
        return;
    g_rx_next_at += g_rx_pace;
    if (g_intreq & H_INTF_RBF) {
        g_rx_ovrun = 1;                     /* and this byte is gone */
    } else {
        g_rx_pos = g_rx_arrive;
        g_intreq |= H_INTF_RBF;
        irq_update();
    }
    g_rx_arrive++;
}

/* Called once per instruction: raise TBE when the buffer frees, so a ring
 * buffer driven by the level 1 interrupt can actually be tested. The
 * power-on state, where Paula has TBE set before anything is sent, is not
 * modelled - TBE is only raised as a consequence of a transmission. */
static void serial_tx_tick(void)
{
    if (!g_tbe_signalled && g_cycles >= g_tbe_at) {
        g_tbe_signalled = 1;
        g_intreq |= H_INTF_TBE;
        irq_update();
    }
}

/* --- vertical blank --------------------------------------------------------
 *
 * Off unless a test asks. The period is in CPU cycles and deliberately has
 * nothing to do with 50Hz: a scheduler test wants hundreds of ticks inside
 * its cycle budget, and wants them to land at awkward places. */
static uint64_t g_vbl_period;
static uint64_t g_vbl_next;

static void vbl_tick(void)
{
    if (g_vbl_period && g_cycles >= g_vbl_next) {
        g_vbl_next = g_cycles + g_vbl_period;
        g_intreq |= H_INTF_VERTB;
        irq_update();
    }
}

void h_vbl_every(uint64_t cycles)
{
    g_vbl_period = cycles;
    g_vbl_next   = g_cycles + cycles;
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
    if (g_slow_size != 0 &&
        addr >= H_SLOW_BASE && addr < H_SLOW_BASE + g_slow_decode) {
        if (writable) *writable = 1;
        /* Partial decoding: a board that only decodes g_slow_size bytes
         * answers to every multiple of it up to g_slow_decode. */
        return g_slow + ((addr - H_SLOW_BASE) % g_slow_size);
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

/*
 * Zorro II expansion space above the fast RAM the model provides.
 *
 * An unpopulated Zorro bus floats high. That is not a detail: detect_fast_ram
 * sizes memory by writing one megabyte past the end and failing the
 * read-back, so without a floating bus the probe hits unmapped memory, the
 * harness calls it a fault, and the routine cannot be tested at all - which
 * is exactly where it stood before this existed.
 */
#define H_ZORRO_END 0x00A00000u

#define ZORRO_BASE   0xE80000u
#define ZORRO_END    0xE90000u

static int floating_bus(uint32_t addr)
{
    if (addr >= H_FAST_BASE + H_FAST_SIZE && addr < H_ZORRO_END)
        return 1;

    /* The trapdoor region, wherever the board does not answer. On hardware
     * this floats to whatever the chipset last drove onto the chip bus - a
     * value that changes between reads - rather than floating high like the
     * Zorro bus. detect_slow_ram writes before it reads for exactly that
     * reason, so a constant here does not flatter it. */
    if (addr >= H_SLOW_BASE + g_slow_decode && addr < H_SLOW_LIMIT)
        return 1;

    /* Expansion space above the one autoconfig slot at $E80000. Zorro II
     * config space is only $E80000-$E8FFFF and every card answers there in
     * turn, but configure_zorro_ii walks a2 up by $10000 per card and so
     * reads $E90000 after the first. On hardware that floats and ends the
     * scan; here it would otherwise look like a fault. */
    if (addr >= ZORRO_END && addr < 0xF00000u)
        return 1;

    return 0;
}

static uint32_t floating_value(int size)
{
    if (size == 1) return 0xFFu;
    if (size == 2) return 0xFFFFu;
    return 0xFFFFFFFFu;
}

/* ------------------------------------------------- Zorro II autoconfig ---
 *
 * One card slot, enough to drive configure_zorro_ii through its memory and
 * I/O paths. Without it the space reads $FF everywhere, which is the "no
 * card" answer and the only case that could be tested.
 *
 * Nibble packed, as the bus is: logical byte register n appears at offset
 * n*4 with its high nibble in bits 15-12, and at n*4+2 with its low nibble
 * in the same place. Register 0 (er_Type) reads straight; every other
 * register is presented inverted, which is why the ROM un-inverts them.
 *
 * Writing the low half of the base address at $48 is the trigger: the card
 * relocates and stops answering here, so a rescan sees an empty slot. The
 * shut-up register at $4C does the same without a relocation.
 */
#define ZORRO_REGS   64

static int      g_zorro_present;
static uint8_t  g_zorro_reg[ZORRO_REGS];
static int      g_zorro_done;        /* relocated or shut up */
static uint32_t g_zorro_base_written;
static int      g_zorro_shutup;

static uint32_t zorro_read(uint32_t addr)
{
    uint32_t off = addr - ZORRO_BASE;
    unsigned reg = off / 4;
    uint8_t  v;

    if (!g_zorro_present || g_zorro_done || reg >= ZORRO_REGS)
        return 0xFFFFu;

    v = g_zorro_reg[reg];
    if (reg != 0)
        v = (uint8_t)~v;                 /* everything but er_Type inverts */

    return (uint32_t)(((off & 2) ? (v & 0x0F) : (v >> 4)) & 0x0F) << 12;
}

static void zorro_write(uint32_t addr, uint32_t val)
{
    uint32_t off = addr - ZORRO_BASE;

    if (!g_zorro_present || g_zorro_done)
        return;

    /* Base address nibbles land in the high half of a byte write. */
    switch (off) {
    case 0x44: g_zorro_base_written =
                   (g_zorro_base_written & 0x0FFFFFFFu) | ((val & 0xF0u) << 24);
               break;
    case 0x46: g_zorro_base_written =
                   (g_zorro_base_written & 0xF0FFFFFFu) | ((val & 0xF0u) << 20);
               break;
    case 0x4A: g_zorro_base_written =
                   (g_zorro_base_written & 0xFFF0FFFFu) | ((val & 0xF0u) << 12);
               break;
    case 0x48: g_zorro_base_written =
                   (g_zorro_base_written & 0xFF0FFFFFu) | ((val & 0xF0u) << 16);
               g_zorro_done = 1;         /* this write is the trigger */
               break;
    case 0x4C: g_zorro_shutup = 1;
               g_zorro_done   = 1;
               break;
    default:   break;
    }
}

void h_attach_zorro(uint8_t er_type, uint8_t er_flags)
{
    int i;
    for (i = 0; i < ZORRO_REGS; i++)
        g_zorro_reg[i] = 0;
    g_zorro_reg[0] = er_type;            /* er_Type  */
    g_zorro_reg[2] = er_flags;           /* er_Flags */
    g_zorro_present = 1;
    g_zorro_done = 0;
    g_zorro_shutup = 0;
    g_zorro_base_written = 0;
}

void     h_detach_zorro(void)   { g_zorro_present = 0; }

/* --- trapdoor / slow RAM ------------------------------------------------ */

void h_attach_slow_ram(uint32_t size)
{
    h_attach_slow_ram_mirrored(size, size);
}

void h_attach_slow_ram_mirrored(uint32_t size, uint32_t decode)
{
    if (size == 0) {
        h_detach_slow_ram();
        return;
    }
    if (decode < size)
        decode = size;
    if (H_SLOW_BASE + decode > H_SLOW_LIMIT)
        decode = H_SLOW_LIMIT - H_SLOW_BASE;

    if (g_slow == NULL)
        g_slow = calloc(1, H_SLOW_LIMIT - H_SLOW_BASE);
    memset(g_slow, 0, size);
    g_slow_size   = size;
    g_slow_decode = decode;
}

void h_detach_slow_ram(void)
{
    g_slow_size = g_slow_decode = 0;
}
uint32_t h_zorro_base(void)     { return g_zorro_base_written; }
int      h_zorro_configured(void) { return g_zorro_done && !g_zorro_shutup; }
int      h_zorro_shut_up(void)  { return g_zorro_shutup; }

/* ------------------------------------------------- CIA-A and the keyboard
 *
 * Enough 8520 to write a keyboard driver against: the interrupt control
 * register, timer A, the serial data register and its direction bit. Not
 * timer B, not the TOD clock, not the ports; and CIA-B is still a stub.
 *
 * ICR is the part worth getting right. Reading it returns the pending flags
 * AND CLEARS THEM ALL - so whoever reads it on behalf of one device has just
 * acknowledged every other device on the chip. The CIA's interrupt line
 * feeds INTREQ PORTS, and it is a level: clearing PORTS in Paula while the
 * CIA still has a flag set just sets it again.
 *
 * The keyboard sends a code into SDR and then waits to be told it arrived:
 * the computer pulls KDAT low by switching the serial port to output, and
 * must hold it for at least 85 microseconds before switching back. Real
 * keyboards tolerate less; the specification does not, and code timed by a
 * delay loop on a 7MHz 68000 is code that fails on a 68060. The model
 * measures the pulse and counts the short ones.
 *
 * Timer A counts the E clock, which is the CPU clock divided by ten. */
#define CIAA_BASE   0xBFE001u
#define CIA_TALO    4
#define CIA_TAHI    5
#define CIA_SDR     12
#define CIA_ICR     13
#define CIA_CRA     14

#define CIA_ICR_TA  0x01u
#define CIA_ICR_SP  0x08u
#define CRA_START   0x01u
#define CRA_ONESHOT 0x08u
#define CRA_LOAD    0x10u
#define CRA_SPMODE  0x40u

#define KBD_HANDSHAKE_MIN 603u      /* 85us of 7.09MHz */
#define KBD_GAP           3000u     /* between a handshake and the next code */

static uint8_t  g_cia_icr, g_cia_mask, g_cia_cra, g_cia_sdr;
static uint16_t g_cia_ta, g_cia_ta_latch;
static uint64_t g_cia_ta_frac;      /* CPU cycles not yet a whole E tick */
static uint64_t g_cia_last;

static uint8_t  g_kbd_q[256];
static size_t   g_kbd_len, g_kbd_pos;
static enum { KBD_IDLE, KBD_SENT, KBD_HANDSHAKING } g_kbd_state;
static uint64_t g_kbd_at;           /* next send, or when the pulse began */
static unsigned g_kbd_ok, g_kbd_short;

static void irq_update(void);

static int cia_irq(void) { return (g_cia_icr & g_cia_mask & 0x1Fu) != 0; }

static void cia_raise(uint8_t flag)
{
    g_cia_icr |= flag;
    if (cia_irq()) {
        g_intreq |= H_INTF_PORTS;
        irq_update();
    }
}

static void cia_tick(void)
{
    uint64_t elapsed = g_cycles - g_cia_last;
    g_cia_last = g_cycles;

    if (g_cia_cra & CRA_START) {
        uint64_t e;
        g_cia_ta_frac += elapsed;
        e = g_cia_ta_frac / 10;
        g_cia_ta_frac %= 10;
        if (e > g_cia_ta) {
            g_cia_ta = g_cia_ta_latch;
            if (g_cia_cra & CRA_ONESHOT)
                g_cia_cra &= (uint8_t)~CRA_START;
            cia_raise(CIA_ICR_TA);
        } else {
            g_cia_ta = (uint16_t)(g_cia_ta - e);
        }
    }

    if (g_kbd_state == KBD_IDLE && g_kbd_pos < g_kbd_len && g_cycles >= g_kbd_at) {
        uint8_t code = g_kbd_q[g_kbd_pos++];
        /* On the wire: bits 6..0 then the up/down bit, all inverted. */
        g_cia_sdr = (uint8_t)~((code << 1) | (code >> 7));
        g_kbd_state = KBD_SENT;
        cia_raise(CIA_ICR_SP);
    }
}

static uint32_t cia_read(uint32_t addr)
{
    switch ((addr - CIAA_BASE) >> 8) {
        case CIA_TALO: return g_cia_ta & 0xFF;
        case CIA_TAHI: return g_cia_ta >> 8;
        case CIA_SDR:  return g_cia_sdr;
        case CIA_CRA:  return g_cia_cra;
        case CIA_ICR: {
            uint8_t v = (uint8_t)(g_cia_icr | (cia_irq() ? 0x80u : 0));
            g_cia_icr = 0;                  /* all of them, whoever asked */
            return v;
        }
        default: return 0;
    }
}

static void cia_write(uint32_t addr, uint8_t val)
{
    switch ((addr - CIAA_BASE) >> 8) {
        case CIA_TALO:
            g_cia_ta_latch = (uint16_t)((g_cia_ta_latch & 0xFF00u) | val);
            break;
        case CIA_TAHI:
            g_cia_ta_latch = (uint16_t)((g_cia_ta_latch & 0x00FFu) | (val << 8));
            /* One-shot and stopped: writing the high byte loads and starts. */
            if ((g_cia_cra & CRA_ONESHOT) && !(g_cia_cra & CRA_START)) {
                g_cia_ta = g_cia_ta_latch;
                g_cia_ta_frac = 0;
                g_cia_cra |= CRA_START;
            }
            break;
        case CIA_ICR:
            if (val & 0x80u) g_cia_mask |= (uint8_t)(val & 0x1Fu);
            else             g_cia_mask &= (uint8_t)~val;
            if (cia_irq()) { g_intreq |= H_INTF_PORTS; irq_update(); }
            break;
        case CIA_CRA: {
            uint8_t was = g_cia_cra;
            if (val & CRA_LOAD) { g_cia_ta = g_cia_ta_latch; g_cia_ta_frac = 0; }
            g_cia_cra = (uint8_t)(val & ~CRA_LOAD);

            if (!(was & CRA_SPMODE) && (val & CRA_SPMODE) && g_kbd_state == KBD_SENT) {
                g_kbd_state = KBD_HANDSHAKING;
                g_kbd_at = g_cycles;
            } else if ((was & CRA_SPMODE) && !(val & CRA_SPMODE) &&
                       g_kbd_state == KBD_HANDSHAKING) {
                if (g_cycles - g_kbd_at >= KBD_HANDSHAKE_MIN) g_kbd_ok++;
                else                                          g_kbd_short++;
                g_kbd_state = KBD_IDLE;
                g_kbd_at = g_cycles + KBD_GAP;
            }
            break;
        }
        default: break;
    }
}

void h_key(uint8_t code)
{
    if (g_kbd_len < sizeof g_kbd_q)
        g_kbd_q[g_kbd_len++] = code;
}

unsigned h_kbd_handshakes(void)       { return g_kbd_ok; }
unsigned h_kbd_short_handshakes(void) { return g_kbd_short; }
int      h_kbd_idle(void)             { return g_kbd_state == KBD_IDLE && g_kbd_pos == g_kbd_len; }

static void cia_reset(void)
{
    g_cia_icr = g_cia_mask = g_cia_cra = 0;
    g_cia_sdr = 0xFF;
    g_cia_ta = g_cia_ta_latch = 0xFFFF;
    g_cia_ta_frac = 0;
    g_cia_last = g_cycles;
    g_kbd_len = g_kbd_pos = 0;
    g_kbd_state = KBD_IDLE;
    g_kbd_at = 0;
    g_kbd_ok = g_kbd_short = 0;
}

static int is_ciaa(uint32_t addr)
{
    return addr >= CIAA_BASE && addr <= CIAA_BASE + 0xF00u && ((addr - CIAA_BASE) & 0xFF) == 0;
}

/* Regions we model well enough not to hang, but do not implement. Reads
 * return a value that means "nothing here"; writes are discarded. */
static int stub_region(uint32_t addr, uint32_t *read_value)
{
    /* CIA-A and CIA-B. Reset code writes control and interrupt registers. */
    if (addr >= 0xBFD000u && addr <= 0xBFEF01u) { *read_value = 0x00; return 1; }

    return 0;
}

/* ------------------------------------------------------- Paula interrupts */

#define REG_INTENAR 0x01Cu
#define REG_INTREQR 0x01Eu
#define REG_INTENA  0x09Au
#define REG_INTREQ  0x09Cu

static uint16_t g_intena;
static int      g_irq_forced;      /* -1 = not forced */

/* Which CPU level each INTREQ bit raises. Index is the bit number; entries
 * past EXTER are unused. Level 7 is the NMI line and is not in here - it does
 * not come from Paula. */
static const int g_int_level[14] = {
    1, 1, 1,        /* TBE, DSKBLK, SOFTINT */
    2,              /* PORTS */
    3, 3, 3,        /* COPER, VERTB, BLIT */
    4, 4, 4, 4,     /* AUD0-3 */
    5, 5,           /* RBF, DSKSYN */
    6               /* EXTER */
};

/* Highest level with a bit both requested and enabled. Zero unless the master
 * enable is on, which is what makes `move.w #$7FFF,INTENA` at reset stick. */
static int irq_level_from_paula(void)
{
    uint16_t active;
    int b, level = 0;

    if (!(g_intena & H_INTF_INTEN))
        return 0;

    active = (uint16_t)(g_intreq & g_intena & 0x3FFFu);
    for (b = 0; b < 14; b++)
        if ((active & (1u << b)) && g_int_level[b] > level)
            level = g_int_level[b];
    return level;
}

/* Recompute and present the level. Called on every write to either register,
 * so an interrupt the handler has not acked stays asserted and re-fires on
 * RTE, exactly as on hardware. */
static void irq_update(void)
{
    int level = g_irq_forced >= 0 ? g_irq_forced : irq_level_from_paula();
    m68k_set_irq((unsigned int)level);
}

/* Answer the interrupt acknowledge cycle the way UAE's cycle-exact 68000
 * does: as a byte read of $FFFFF1 + 2*level, the address the CPU drives
 * during IACK, which lands in the last 16 bytes of the ROM. The byte is the
 * vector NUMBER. Kickstart ends in 0018 0019 ... 001F so that read yields
 * the autovector; a ROM without the table sends every interrupt through
 * vector 0. Returning M68K_INT_ACK_AUTOVECTOR here instead is what let that
 * exact bug pass every test and then fail under FS-UAE. */
static int int_ack(int level)
{
    return g_rom[H_ROM_SIZE - 15 + 2 * (level & 7)];
}

static uint16_t setclr(uint16_t cur, uint16_t val)
{
    if (val & H_INTF_SETCLR)
        return (uint16_t)(cur | (val & 0x7FFFu));
    return (uint16_t)(cur & ~(val & 0x7FFFu));
}

uint16_t h_intena(void) { return g_intena; }
uint16_t h_intreq(void) { return g_intreq; }

void h_write_intena(uint16_t val)
{
    g_intena = setclr(g_intena, val);
    irq_update();
}

void h_write_intreq(uint16_t val)
{
    uint16_t before = g_intreq;

    g_intreq = setclr(g_intreq, val);

    /* Clearing RBF is the acknowledgement that consumes the byte. Until it
     * happens SERDATR keeps reporting the same character, which is what
     * hardware does and what the old model papered over. */
    if ((before & H_INTF_RBF) && !(g_intreq & H_INTF_RBF)) {
        g_rx_ovrun = 0;
        if (!g_rx_pace && g_rx_pos < g_rx_len)
            g_rx_pos++;
    }

    /* The CIA's interrupt line is a level: PORTS cannot be cleared from
     * under a CIA that still has an enabled flag set. */
    if (cia_irq())
        g_intreq |= H_INTF_PORTS;

    serial_rx_latch();
    irq_update();
}

void h_raise(uint16_t bits) { h_write_intreq((uint16_t)(H_INTF_SETCLR | bits)); }

int h_irq_level(void)
{
    return g_irq_forced >= 0 ? g_irq_forced : irq_level_from_paula();
}

void h_irq_force(int level)
{
    g_irq_forced = (level > 0) ? level : -1;
    irq_update();
}

void     h_set_sr(uint16_t sr) { m68k_set_reg(M68K_REG_SR, sr); }
uint16_t h_get_sr(void) { return (uint16_t)m68k_get_reg(NULL, M68K_REG_SR); }

/* ------------------------------------------------------------- Gayle IDE */

#define IDE_BASE     0xDA0000u
#define IDE_END      0xDA2000u
#define IDE_DATA     0xDA0002u
#define IDE_ERROR    0xDA0006u
#define IDE_NSECTOR  0xDA000Au
#define IDE_SECTOR   0xDA000Eu
#define IDE_LCYL     0xDA0012u
#define IDE_HCYL     0xDA0016u
#define IDE_SELECT   0xDA001Au
#define IDE_STATUS   0xDA001Eu        /* command on write */

#define ATA_ERR      0x01
#define ATA_DRQ      0x08
#define ATA_DRDY     0x40
#define ATA_BSY      0x80
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_FLUSH    0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define NO_DRIVE     0x7F             /* what an empty bus floats to */

static uint8_t  *g_disk;
static uint32_t  g_disk_sectors;

static uint8_t   g_ata_lba[4];
static uint8_t   g_ata_nsector;
static uint8_t   g_ata_select;
static uint8_t   g_ata_error;
static uint32_t  g_ata_next_lba;
static int       g_ata_left;
static uint8_t   g_ata_buf[512];
static int       g_ata_pos;
static int       g_ata_drq;
static int       g_ata_err;
static int       g_ata_writing;     /* DRQ means "give me data", not "take it" */
static int       g_ata_identify;    /* the buffer is IDENTIFY data, not a sector */
static unsigned  g_ata_commands, g_ata_sectors_read, g_ata_sectors_written;

int h_attach_disk(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;

    h_detach_disk();

    if (!f) {
        fprintf(stderr,
            "harness: cannot open disk image '%s'\n"
            "         generate it first: python3 tests/mkdisk.py tests/build\n",
            path);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || (size % 512) != 0) {
        fprintf(stderr, "harness: disk image '%s' is not a whole number of sectors\n",
                path);
        fclose(f);
        return 2;
    }
    g_disk = malloc((size_t)size);
    if (!g_disk || fread(g_disk, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "harness: short read on disk image '%s'\n", path);
        fclose(f);
        free(g_disk);
        g_disk = NULL;
        return 2;
    }
    fclose(f);
    g_disk_sectors = (uint32_t)(size / 512);
    return 0;
}

void h_detach_disk(void)
{
    free(g_disk);
    g_disk = NULL;
    g_disk_sectors = 0;
    g_ata_commands = g_ata_sectors_read = g_ata_sectors_written = 0;
    g_ata_drq = g_ata_err = g_ata_left = g_ata_pos = 0;
    g_ata_error = 0;
    memset(g_ata_lba, 0, sizeof g_ata_lba);
    g_ata_nsector = 0;
    g_ata_select = 0;
}

uint32_t h_disk_sectors(void) { return g_disk_sectors; }

unsigned h_disk_commands(void)        { return g_ata_commands; }
unsigned h_disk_sectors_read(void)    { return g_ata_sectors_read; }
unsigned h_disk_sectors_written(void) { return g_ata_sectors_written; }

int h_disk_read(uint32_t lba, void *buf512)
{
    if (!g_disk || lba >= g_disk_sectors) return -1;
    memcpy(buf512, g_disk + (size_t)lba * 512, 512);
    return 0;
}

static void ata_load_next_sector(void)
{
    if (g_ata_left <= 0 || g_ata_next_lba >= g_disk_sectors) {
        g_ata_drq = 0;
        return;
    }
    memcpy(g_ata_buf, g_disk + (size_t)g_ata_next_lba * 512, 512);
    g_ata_next_lba++;
    g_ata_pos = 0;
    g_ata_drq = 1;
}

static void ata_command(uint8_t cmd)
{
    uint32_t lba;
    int count;

    g_ata_err = 0;
    g_ata_error = 0;
    g_ata_drq = 0;

    g_ata_writing = 0;
    g_ata_identify = 0;
    g_ata_commands++;

    if (cmd == ATA_CMD_FLUSH)
        return;                                   /* nothing is ever unflushed */
    if (cmd == ATA_CMD_IDENTIFY) {
        /* Words are little-endian, as ATA defines them, and only the ones
         * a driver needs: 49 says LBA is supported, 60-61 are the sector
         * count. */
        memset(g_ata_buf, 0, sizeof g_ata_buf);
        g_ata_buf[49 * 2 + 1] = 0x02;
        g_ata_buf[60 * 2]     = (uint8_t)g_disk_sectors;
        g_ata_buf[60 * 2 + 1] = (uint8_t)(g_disk_sectors >> 8);
        g_ata_buf[61 * 2]     = (uint8_t)(g_disk_sectors >> 16);
        g_ata_buf[61 * 2 + 1] = (uint8_t)(g_disk_sectors >> 24);
        g_ata_identify = 1;
        g_ata_left = 1;
        g_ata_pos = 0;
        g_ata_drq = 1;
        return;
    }
    if (cmd != ATA_CMD_READ && cmd != ATA_CMD_WRITE) {
        g_ata_err = 1;
        g_ata_error = 0x04;                       /* ABRT */
        return;
    }
    lba = (uint32_t)g_ata_lba[0]
        | ((uint32_t)g_ata_lba[1] << 8)
        | ((uint32_t)g_ata_lba[2] << 16)
        | ((uint32_t)(g_ata_select & 0x0F) << 24);
    count = g_ata_nsector ? g_ata_nsector : 256;  /* 0 means 256 in ATA */

    if (lba + (uint32_t)count > g_disk_sectors) {
        g_ata_err = 1;
        g_ata_error = 0x10;                       /* IDNF */
        return;
    }
    g_ata_next_lba = lba;
    g_ata_left = count;
    if (cmd == ATA_CMD_WRITE) {
        g_ata_writing = 1;
        g_ata_pos = 0;
        g_ata_drq = 1;                            /* ready for the first sector */
        return;
    }
    ata_load_next_sector();
}

static uint32_t ide_read(uint32_t addr, int size)
{
    if (!g_disk)
        return NO_DRIVE;

    if (addr == IDE_DATA && size == 2) {
        uint32_t v;
        if (!g_ata_drq) {
            fault("IDE data read with DRQ clear (pc $%06X)",
                  m68k_get_reg(NULL, M68K_REG_PPC));
            return 0;
        }
        /* High byte first, so a move.w into memory reproduces disk order. */
        if (g_ata_writing) {
            fault("IDE data read during a WRITE command (pc $%06X)",
                  m68k_get_reg(NULL, M68K_REG_PPC));
            return 0;
        }
        v = ((uint32_t)g_ata_buf[g_ata_pos] << 8) | g_ata_buf[g_ata_pos + 1];
        g_ata_pos += 2;
        if (g_ata_pos >= 512) {
            g_ata_left--;
            if (g_ata_identify) {
                g_ata_drq = 0;
            } else {
                g_ata_sectors_read++;
                ata_load_next_sector();
            }
        }
        return v;
    }
    if (addr == IDE_STATUS)
        return (uint32_t)(ATA_DRDY
                          | (g_ata_drq ? ATA_DRQ : 0)
                          | (g_ata_err ? ATA_ERR : 0));
    if (addr == IDE_ERROR)   return g_ata_error;
    if (addr == IDE_NSECTOR) return g_ata_nsector;
    if (addr == IDE_SECTOR)  return g_ata_lba[0];
    if (addr == IDE_LCYL)    return g_ata_lba[1];
    if (addr == IDE_HCYL)    return g_ata_lba[2];
    if (addr == IDE_SELECT)  return g_ata_select;
    return 0;
}

static void ide_write(uint32_t addr, int size, uint32_t val)
{
    if (!g_disk)
        return;

    if (addr == IDE_DATA && size == 2) {
        if (!g_ata_drq || !g_ata_writing) {
            fault("IDE data write with no WRITE command waiting (pc $%06X)",
                  m68k_get_reg(NULL, M68K_REG_PPC));
            return;
        }
        g_ata_buf[g_ata_pos]     = (uint8_t)(val >> 8);
        g_ata_buf[g_ata_pos + 1] = (uint8_t)val;
        g_ata_pos += 2;
        if (g_ata_pos >= 512) {
            memcpy(g_disk + (size_t)g_ata_next_lba * 512, g_ata_buf, 512);
            g_ata_sectors_written++;
            g_ata_next_lba++;
            g_ata_pos = 0;
            if (--g_ata_left <= 0) {
                g_ata_drq = 0;
                g_ata_writing = 0;
            }
        }
        return;
    }

    if      (addr == IDE_NSECTOR) g_ata_nsector = (uint8_t)val;
    else if (addr == IDE_SECTOR)  g_ata_lba[0]  = (uint8_t)val;
    else if (addr == IDE_LCYL)    g_ata_lba[1]  = (uint8_t)val;
    else if (addr == IDE_HCYL)    g_ata_lba[2]  = (uint8_t)val;
    else if (addr == IDE_SELECT)  g_ata_select  = (uint8_t)val;
    else if (addr == IDE_STATUS)  ata_command((uint8_t)val);   /* command port */
}

static uint32_t custom_read(uint32_t addr, int size)
{
    uint32_t off = addr - CUSTOM_BASE;

    /* SERDATR. A word read is what serial_get_char uses to take the
     * character, so that is where the receive buffer is consumed. The byte
     * reads are serial_put_char's and serial_get_char's btst polling the
     * high half - those must not consume anything. */
    if (off == REG_SERDATR && size == 2)
        return serdatr_value();
    if (off == REG_SERDATR && size == 1) return serdatr_value() >> 8;
    if (off == REG_SERDATR + 1 && size == 1) return serdatr_value() & 0xFF;

    if (off == REG_INTENAR) return g_intena;
    if (off == REG_INTREQR) return g_intreq;

    /* Everything else in custom space reads as zero. DMACONR and VPOSR would
     * go here if a test needed them. */
    return 0;
}

static uint16_t g_custom_last[0x100];
static unsigned g_custom_count[0x100];

static void custom_note(uint32_t off, uint16_t val)
{
    if (off < 0x200) {
        g_custom_last[off >> 1] = val;
        g_custom_count[off >> 1]++;
    }
}

static void custom_write(uint32_t addr, int size, uint32_t val)
{
    uint32_t off = addr - CUSTOM_BASE;
    (void)size;

    if (off == REG_SERDAT) {
        /* Paula has one buffer byte. Writing before TBE destroys the byte
         * still waiting in it, silently. The capture keeps both so the text
         * stays readable; the count is what a test asserts on. */
        if (g_cycles < g_tbe_at)
            g_tx_overruns++;
        if (g_tx_len + 1 < sizeof g_tx)
            g_tx[g_tx_len++] = (char)(val & 0xFF);
        g_tbe_at  = g_cycles + g_tbe_cycles;
        g_tsre_at = g_cycles + g_tsre_cycles;
        g_tbe_signalled = 0;
        return;
    }
    if (off == REG_INTENA) { h_write_intena((uint16_t)val); return; }
    if (off == REG_INTREQ) { h_write_intreq((uint16_t)val); return; }

    /* Everything else - SERPER, COLOR00, DMACON, the bitplane and copper
     * registers - does nothing here, but is remembered: the display tests
     * want to know what the chipset was told, and when. A long write is two
     * registers, as it is on the bus. */
    if (size == 4) {
        custom_note(off, (uint16_t)(val >> 16));
        custom_note(off + 2, (uint16_t)val);
    } else {
        custom_note(off, (uint16_t)val);
    }
}

uint16_t h_custom(uint32_t reg)        { return reg < 0x200 ? g_custom_last[reg >> 1] : 0; }
unsigned h_custom_writes(uint32_t reg) { return reg < 0x200 ? g_custom_count[reg >> 1] : 0; }

/*
 * A copper, as far as a display test needs one: run a list from the top of
 * the frame down to `line` and report what every register holds there. MOVE
 * and WAIT only; vertical position only. Lines past 255 are reached the way
 * real lists reach them - a wait for $FFDF, after which the 8-bit vertical
 * counter has wrapped and positions are relative to line 256.
 */
int h_copper_at(uint32_t list, int line, uint16_t regs[0x100])
{
    int base = 0, n;

    memset(regs, 0, 0x100 * sizeof regs[0]);
    for (n = 0; n < 4096; n++, list += 4) {
        uint16_t w1 = h_peek16(list), w2 = h_peek16(list + 2);

        if (w1 == 0xFFFF && w2 == 0xFFFE)
            return 0;                           /* end of list */
        if (!(w1 & 1)) {
            regs[(w1 & 0x1FE) >> 1] = w2;       /* MOVE */
        } else if (!(w2 & 1)) {                 /* WAIT */
            if (w1 == 0xFFDF) { base = 256; continue; }
            if (base + (w1 >> 8) > line)
                return 0;
        }
    }
    return -1;                                  /* no end: not a copper list */
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
    if (addr >= IDE_BASE && addr < IDE_END)
        return ide_read(addr, size);
    if (addr >= ZORRO_BASE && addr < ZORRO_END)
        return zorro_read(addr);
    if (is_ciaa(addr))
        return cia_read(addr);
    if (stub_region(addr, &stub))
        return stub;
    if (floating_bus(addr))
        return floating_value(size);
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
    if (addr >= IDE_BASE && addr < IDE_END) {
        ide_write(addr, size, val);
        return;
    }
    if (addr >= ZORRO_BASE && addr < ZORRO_END) {
        zorro_write(addr, val);
        return;
    }
    if (is_ciaa(addr)) {
        cia_write(addr, (uint8_t)val);
        return;
    }
    if (stub_region(addr, &stub))
        return;
    if (floating_bus(addr))
        return;                            /* nothing there to take it */
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

static size_t g_symcap;

static int load_symbols(const char *path, uint32_t bias, const char *prefix)
{
    FILE *f = fopen(path, "r");
    char line[256];
    size_t before = g_nsyms;

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
        if (g_nsyms == g_symcap) {
            g_symcap = g_symcap ? g_symcap * 2 : 256;
            g_syms = realloc(g_syms, g_symcap * sizeof *g_syms);
            if (!g_syms) { fprintf(stderr, "harness: out of memory\n"); exit(2); }
        }
        snprintf(g_syms[g_nsyms].name, sizeof g_syms[g_nsyms].name, "%s%s",
                 prefix, name);
        g_syms[g_nsyms].addr = (uint32_t)addr + bias;
        g_nsyms++;
    }
    fclose(f);

    if (g_nsyms == before) {
        fprintf(stderr,
            "harness: symbol file '%s' added no symbols\n"
            "         tests/mksym.py may not understand this vasm's listing format\n",
            path);
        exit(2);
    }
    return 0;
}

int h_add_symbols(const char *path, uint32_t bias)
{
    return load_symbols(path, bias, "");
}

int h_add_symbols_prefixed(const char *path, uint32_t bias, const char *prefix)
{
    return load_symbols(path, bias, prefix);
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

/* ------------------------------------------------------------------ modules */

#define MAX_MODULES 8

typedef struct { uint8_t *data; size_t len; uint32_t addr; } module_t;

static module_t g_modules[MAX_MODULES];
static int      g_nmodules;

static void apply_modules(void)
{
    int i;
    for (i = 0; i < g_nmodules; i++) {
        uint8_t *p = raw_ptr(g_modules[i].addr, NULL);
        if (p) memcpy(p, g_modules[i].data, g_modules[i].len);
    }
}

int h_load_module(const char *path, uint32_t addr)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;

    if (g_nmodules == MAX_MODULES) {
        fprintf(stderr, "harness: too many modules\n");
        return 2;
    }
    if (!f) {
        fprintf(stderr,
            "harness: cannot open module '%s'\n"
            "         build it first: make -C tests\n", path);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || raw_ptr(addr, NULL) == NULL ||
        raw_ptr(addr + (uint32_t)size - 1, NULL) == NULL) {
        fprintf(stderr, "harness: module '%s' does not fit at $%06X\n", path, addr);
        fclose(f);
        return 2;
    }
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "harness: short read on module '%s'\n", path);
        fclose(f);
        free(buf);
        return 2;
    }
    fclose(f);

    g_modules[g_nmodules].data = buf;
    g_modules[g_nmodules].len  = (size_t)size;
    g_modules[g_nmodules].addr = addr;
    g_nmodules++;

    apply_modules();
    return 0;
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

#define H_DEFAULT_BUDGET 20000000u
static uint64_t g_budget = H_DEFAULT_BUDGET;

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

/*
 * Where a test may put a heap for the kernel's allocator: the first 64KB
 * boundary past the end of the kernel image, .bss included.
 *
 * This used to be a constant, $210000, in every kernel test. The kernel's
 * .bss grew past it - the block cache alone is 128KB - and from then on
 * every test heap lay on top of the kernel's own variables, and every test
 * passed anyway, until a change in code size moved things enough for one to
 * crash. A test must not know where the kernel ends; it must ask.
 */
uint32_t h_kernel_heap(uint32_t size)
{
    uint32_t base = (h_sym("kernel:__end") + 0xFFFFu) & ~0xFFFFu;

    if (base + size > H_SCRATCH_BASE) {
        fprintf(stderr,
            "harness: no room for a %u byte test heap: the kernel ends at $%06X\n"
            "         and scratch memory starts at $%06X. Make the machine bigger.\n",
            size, h_sym("kernel:__end"), H_SCRATCH_BASE);
        exit(2);
    }
    return base;
}

uint32_t h_get_pc(void) { return m68k_get_reg(NULL, M68K_REG_PC); }

h_result h_resume(void)
{
    return h_run(m68k_get_reg(NULL, M68K_REG_PC));
}

/* Swap the CPU core. Musashi wants a reset after a type change, and reset
 * reloads SSP and PC from vectors 0 and 1, which here are RAM - so the
 * caller gets the same "SR $2700, registers unset" state h_reset leaves. */
static unsigned g_cpu_type = M68K_CPU_TYPE_68000;

void h_set_cpu(int model)
{
    switch (model) {
        case 68000: g_cpu_type = M68K_CPU_TYPE_68000; break;
        case 68010: g_cpu_type = M68K_CPU_TYPE_68010; break;
        case 68020: g_cpu_type = M68K_CPU_TYPE_68EC020; break;  /* 24-bit bus */
        case 68030: g_cpu_type = M68K_CPU_TYPE_68EC030; break;
        case 68040: g_cpu_type = M68K_CPU_TYPE_68EC040; break;
        default:
            fprintf(stderr, "harness: no CPU model %d\n", model);
            exit(2);
    }
    m68k_set_cpu_type(g_cpu_type);
    m68k_pulse_reset();
    m68k_set_irq(0);
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
        {
            uint64_t n = (uint64_t)m68k_execute(1);
            r.cycles += n;
            g_cycles += n;
            serial_tx_tick();
            serial_rx_tick();
            vbl_tick();
            cia_tick();
        }
    }

    /* A fault during an otherwise clean return still fails the call. */
    if (r.status == H_OK && g_fault_count) {
        r.status = H_FAULT;
        snprintf(r.detail, sizeof r.detail, "%s", g_fault_detail);
    }
    return r;
}

/* ---------------------------------------------------------------- lifecycle */

const char *h_serial(void)     { g_tx[g_tx_len] = '\0'; return g_tx; }
size_t      h_serial_len(void) { return g_tx_len; }
void        h_serial_clear(void) { g_tx_len = 0; g_tx[0] = '\0'; }
unsigned    h_serial_overruns(void) { return g_tx_overruns; }

void h_serial_set_timing(uint64_t tbe_cycles, uint64_t tsre_cycles)
{
    g_tbe_cycles  = tbe_cycles;
    g_tsre_cycles = tsre_cycles;
}

void h_serial_input(const char *s)
{
    size_t n = strlen(s);
    if (n > sizeof g_rx) n = sizeof g_rx;
    memcpy(g_rx, s, n);
    g_rx_len = n;
    g_rx_pos = 0;
    g_rx_arrive = 0;
    g_rx_next_at = g_cycles + g_rx_pace;
    serial_rx_latch();
    irq_update();
}

void h_serial_rx_pacing(uint64_t cycles) { g_rx_pace = cycles; }

void h_reset(void)
{
    int v;

    g_rx_pace = 0;
    g_rx_ovrun = 0;
    cia_reset();
    memset(g_custom_last, 0, sizeof g_custom_last);
    memset(g_custom_count, 0, sizeof g_custom_count);

    memset(g_chip, 0, H_CHIP_SIZE);
    memset(g_fast, 0, H_FAST_SIZE);

    h_detach_disk();

    g_tx_len = 0;
    g_rx_len = g_rx_pos = 0;
    g_intena = g_intreq = 0;
    g_irq_forced = -1;
    g_zorro_present = 0;
    h_detach_slow_ram();
    g_zorro_done = g_zorro_shutup = 0;
    g_zorro_base_written = 0;
    g_cycles = 0;
    g_vbl_period = 0;
    g_tbe_at = g_tsre_at = 0;
    g_tbe_cycles  = 64;
    g_tsre_cycles = 128;
    g_tx_overruns = 0;
    g_tbe_signalled = 1;       /* idle transmitter, nothing sent yet */
    g_budget = H_DEFAULT_BUDGET;   /* a test that lowered it must not leak it */
    g_fault_count = 0;
    g_fault_detail[0] = '\0';
    g_scratch_next = H_SCRATCH_BASE;

    apply_modules();        /* reset cleared RAM; put the blobs back */

    /* Reset reads SSP from $0 and PC from $4. Give it something sane so the
     * reset itself does not look like a wild fetch. */
    h_poke32(0, H_STACK_TOP);
    h_poke32(4, H_ROM_BASE);

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(int_ack);
    m68k_pulse_reset();
    m68k_set_irq(0);

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

    load_symbols(sym_path, 0, "");
    h_reset();
    return 0;
}

void h_shutdown(void)
{
    int i;
    for (i = 0; i < g_nmodules; i++)
        free(g_modules[i].data);
    g_nmodules = 0;

    h_detach_disk();
    free(g_chip); free(g_fast); free(g_rom); free(g_syms);
    g_chip = g_fast = g_rom = NULL; g_syms = NULL; g_nsyms = 0;
}
