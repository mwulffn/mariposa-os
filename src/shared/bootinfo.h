/*
 * bootinfo.h - what the ROM tells the kernel at handoff
 *
 * The ROM builds one of these at BOOTINFO_ADDR and passes its address in A0.
 * It replaces a handoff that was two bare registers - A0 the memory map, A1
 * the panic entry - with no room for a third fact, which is why the
 * partition the kernel was loaded from was computed by the ROM and then
 * dropped on the floor.
 *
 * Growing it:
 *   - Append fields. Never reorder, resize or remove one.
 *   - Bump BOOTINFO_VERSION when you do. `size` grows by itself.
 *   - A reader uses BOOTINFO_HAS() before touching anything newer than
 *     version 1, so a new kernel on an old ROM sees the field as absent
 *     instead of reading whatever lies past the end of the struct. An old
 *     kernel on a new ROM never looks at what it does not know about.
 *
 * A1 still carries the panic entry as well as the struct does. That is on
 * purpose: it is the one thing the kernel needs in order to complain that
 * the struct is bad.
 *
 * Every field is a long or a pair of shorts, so there is no padding for two
 * compilers to disagree about.
 */
#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "memmap.h"

#define BOOTINFO_MAGIC    0x424F4F54UL  /* 'BOOT' */
#define BOOTINFO_VERSION  2
#define BOOTINFO_ADDR     0x003500UL    /* in the ROM's reserved low 16KB */

/* boot_dev_type */
#define BOOTDEV_NONE      0
#define BOOTDEV_IDE       1             /* Gayle IDE; boot_dev_unit 0 = master */

/* cpu_type. Ordered, so "has a format word in its exception frames" is
 * cpu_type >= CPU_68010. */
#define CPU_68000         0
#define CPU_68010         1
#define CPU_68020         2
#define CPU_68030         3
#define CPU_68040         4
#define CPU_68060         6

/* fpu_type */
#define FPU_NONE          0
#define FPU_6888X         1             /* 68881 or 68882 coprocessor */
#define FPU_68040         4             /* on-chip */
#define FPU_68060         6             /* on-chip */

struct bootinfo {
    unsigned long  magic;               /* BOOTINFO_MAGIC */
    unsigned short version;             /* BOOTINFO_VERSION of the writer */
    unsigned short size;                /* sizeof(struct bootinfo), writer's */

    /* --- version 1 ------------------------------------------------------ */
    struct mem_entry *memmap;           /* MEM_TYPE_END terminated */
    void (*rom_panic)(void);            /* ROM debugger entry */

    unsigned long  kernel_base;         /* where SYSTEM.BIN was loaded */
    unsigned long  kernel_size;         /* bytes read from disk; no .bss */
    unsigned long  stack_top;           /* A7 at entry */

    unsigned short boot_dev_type;       /* BOOTDEV_* */
    unsigned short boot_dev_unit;
    unsigned long  boot_part_lba;       /* first block of the boot partition */
    unsigned long  boot_part_blocks;    /* its length, in 512-byte blocks */

    unsigned short rom_version;         /* ROM_VERSION from the ROM header */
    unsigned short reserved0;           /* keeps the struct longword sized */

    /* --- version 2 ------------------------------------------------------ */
    unsigned short cpu_type;            /* CPU_*; absent means a 68000 */
    unsigned short fpu_type;            /* FPU_* */
};

/* True if the writer's struct is long enough to contain `field`. */
#define BOOTINFO_HAS(bi, field) \
    ((bi)->size >= (unsigned short)((unsigned long)&((struct bootinfo *)0)->field \
                    + sizeof ((struct bootinfo *)0)->field))

#endif /* BOOTINFO_H */
