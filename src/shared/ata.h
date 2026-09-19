/*
 * ata.h - ATA (IDE) protocol, independent of where the registers live
 *
 * The protocol and the transport are separated on purpose. LBA28 PIO reads
 * are identical on Gayle's IDE port, on a CompactFlash adapter hanging off
 * it, and on a CF card in the A600/A1200 PCMCIA slot. What differs is only
 * the address of the task file: Gayle puts its registers at $DA0002 every
 * four bytes, while the PCMCIA I/O window is at $A20000 with the odd 8-bit
 * registers split off to $A30000 - a mapping no base-and-stride pair can
 * describe, which is why the accessor is a function.
 *
 * PCMCIA is not planned. Keeping it possible costs one function pointer.
 *
 * Nothing here blocks forever: every wait is bounded by a spin count.
 */
#ifndef ATA_H
#define ATA_H

/* Task-file registers. 0 is the 16-bit data port and is reached through
 * `data`, not `reg`; 7 reads status and writes command. */
#define ATA_REG_DATA     0
#define ATA_REG_ERROR    1
#define ATA_REG_NSECTOR  2
#define ATA_REG_LBA0     3      /* LBA bits 0-7    */
#define ATA_REG_LBA1     4      /* LBA bits 8-15   */
#define ATA_REG_LBA2     5      /* LBA bits 16-23  */
#define ATA_REG_SELECT   6      /* LBA bits 24-27, drive, mode */
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7

/* Status bits */
#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01

/* A floating bus reads back as $7F - no drive, rather than a busy one. */
#define ATA_NO_DRIVE    0x7F

#define ATA_MAX_SECTORS 256     /* an LBA28 sector count of 0 means 256 */

struct ata_if {
    /* Address of byte-wide task-file register `n`. */
    volatile unsigned char *(*reg)(int n);
    /* The 16-bit data port. */
    volatile unsigned short *data;
};

unsigned char ata_status(const struct ata_if *ifc);
unsigned char ata_error(const struct ata_if *ifc);

/* Non-zero if a drive answers at all. */
int ata_present(const struct ata_if *ifc);

/* Read `count` sectors from `lba` into `buf`, which must be word aligned.
 * Returns 0 on success, -1 on invalid arguments, timeout or drive error. */
int ata_read_sectors(const struct ata_if *ifc, unsigned long lba,
                     unsigned count, void *buf);

#endif /* ATA_H */
