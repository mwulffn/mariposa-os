/*
 * autoconfig.c - Zorro II expansion autoconfig
 *
 * Walks the autoconfig slot, gives each memory board a base address and
 * silences everything else, so that memory detection has something to find.
 * Must run before detect_fast_ram.
 *
 * ROM-only: the kernel inherits the result through the memory map and has no
 * reason to configure a bus that is already configured.
 */
#include "rom.h"

#define ZORRO_BASE      0xE80000UL
#define ZORRO_SLOT_STEP 0x10000UL
#define MAX_CARDS       8

#define FAST_BASE       0x200000UL

/* Autoconfig register byte offsets, as seen by the nibble-packed reader. */
#define ER_TYPE         0x00
#define ER_FLAGS        0x08
#define EC_BASE_HI_HI   0x44
#define EC_BASE_HI_LO   0x46
#define EC_BASE_LO_HI   0x48        /* writing this one relocates the card */
#define EC_BASE_LO_LO   0x4A
#define EC_SHUTUP       0x4C

#define ERT_TYPE_MASK   0xC0
#define ERT_ZORRO_II    0xC0
#define ERT_SIZE_MASK   0x07
#define ERF_MEMORY      0x80

#define CUSTOM_COLOR00  ((volatile unsigned short *)0xDFF180UL)

/*
 * Autoconfig registers are nibble packed: the high nibble of logical byte
 * `off` sits in bits 15-12 at slot+off, and the low nibble in the same bits
 * at slot+off+2. Everything except er_Type is presented inverted.
 */
static unsigned char read_reg(unsigned long slot, unsigned off)
{
    volatile unsigned short *hi = (volatile unsigned short *)(slot + off);
    volatile unsigned short *lo = (volatile unsigned short *)(slot + off + 2);
    unsigned char v;

    v = (unsigned char)((((*hi >> 12) & 0x0F) << 4) | ((*lo >> 12) & 0x0F));
    return (off == ER_TYPE) ? v : (unsigned char)~v;
}

static void write_reg(unsigned long slot, unsigned off, unsigned char nibble)
{
    *(volatile unsigned char *)(slot + off) = (unsigned char)(nibble << 4);
}

/*
 * The base address goes out as four nibbles, high to low, and the write to
 * EC_BASE_LO_HI is the trigger that makes the card move. Order matters.
 */
static void write_base_address(unsigned long slot, unsigned long base)
{
    write_reg(slot, EC_BASE_HI_HI, (unsigned char)((base >> 28) & 0x0F));
    write_reg(slot, EC_BASE_HI_LO, (unsigned char)((base >> 24) & 0x0F));
    write_reg(slot, EC_BASE_LO_LO, (unsigned char)((base >> 16) & 0x0F));
    write_reg(slot, EC_BASE_LO_HI, (unsigned char)((base >> 20) & 0x0F));
}

/*
 * Zorro II size encoding. 000 is the odd one out at 8MB; 001 upwards are
 * 64KB doubling.
 */
static unsigned long size_from_code(unsigned char code)
{
    if (code == 0)
        return 0x800000UL;
    return 0x10000UL << (code - 1);
}

unsigned long rom_configure_zorro_ii(void)
{
    unsigned long slot = ZORRO_BASE;
    unsigned long next_base = FAST_BASE;
    unsigned long first_ram = 0;
    unsigned char previous_type = 0;
    int card;

    *CUSTOM_COLOR00 = 0x0F0F;                   /* magenta: scanning */
    rom_serial_put_string("Autoconfig: Scanning Zorro bus...\n\r");

    for (card = 0; card < MAX_CARDS; card++) {
        unsigned char type, flags;

        *CUSTOM_COLOR00 = 0x00FF;               /* cyan: reading */

        type = read_reg(slot, ER_TYPE);

        /*
         * Three ways a slot turns out to be empty: nothing answers, the bus
         * repeats the previous card's value rather than presenting a new
         * one, or what answers is not a Zorro II board. All three end the
         * scan, and all three say so - including after a card has already
         * been configured, which is where the "No card found" on a normal
         * boot comes from.
         */
        if (type == 0xFF || type == previous_type ||
            (type & ERT_TYPE_MASK) != ERT_ZORRO_II) {
            rom_serial_put_string("  No card found\n\r");
            break;
        }
        previous_type = type;

        flags = read_reg(slot, ER_FLAGS);

        if (flags & ERF_MEMORY) {
            unsigned long args[1];

            *CUSTOM_COLOR00 = 0x0FF0;           /* yellow: memory card */
            rom_serial_put_string("  Memory card found!\n\r");

            if (first_ram == 0)
                first_ram = next_base;

            args[0] = next_base;
            rom_printf("  Allocating at: $%x\n\r", args);

            write_base_address(slot, next_base);
            *CUSTOM_COLOR00 = 0x0F80;           /* orange: relocated */

            next_base += size_from_code((unsigned char)(type & ERT_SIZE_MASK));
        } else {
            *CUSTOM_COLOR00 = 0x0F00;           /* red: I/O card */
            rom_serial_put_string("  I/O card found, shutting up\n\r");
            *(volatile unsigned char *)(slot + EC_SHUTUP) = 0xFF;
        }

        /*
         * Every Zorro II card answers at $E80000 in turn, so this advance is
         * wrong - a second card would be looked for in the wrong place and
         * never found. Carried over from the assembly; on hardware the space
         * above the slot floats, so the scan ends quietly instead of
         * misbehaving. See CLAUDE.md known issues.
         */
        slot += ZORRO_SLOT_STEP;
    }

    if (card >= MAX_CARDS)
        rom_serial_put_string("  Max cards reached\n\r");

    *CUSTOM_COLOR00 = 0x000F;                   /* blue: done */

    {
        unsigned long args[1];
        args[0] = first_ram;
        rom_printf("Autoconfig: Done, first RAM base: $%x\n\r", args);
    }

    return first_ram;
}
