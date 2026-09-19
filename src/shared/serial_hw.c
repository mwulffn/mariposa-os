/*
 * serial_hw.c - Paula UART register primitives
 *
 * See serial_hw.h for why this is the only part of serial that ROM and
 * kernel share.
 */
#include "serial_hw.h"
#include "amiga_hw.h"

/*
 * Status polling reads the HIGH BYTE of SERDATR, not the word.
 *
 * All three status bits live above bit 8, so a byte read reaches them, and
 * it is what the assembly did with `btst #n,SERDATR(a6)`. The width matters:
 * taking a received byte is a word read of SERDATR, and a poll that used a
 * word read would be indistinguishable from taking the byte. Paula does not
 * care, but the test harness models the distinction on purpose (harness.h),
 * and code that polls by consuming is wrong on the face of it.
 */
#define SERDATR_STATUS (*(volatile unsigned char *)0xDFF018)

#define STATF_TSRE (SERDATF_TSRE >> 8)
#define STATF_TBE  (SERDATF_TBE  >> 8)
#define STATF_RBF  (SERDATF_RBF  >> 8)

void serial_hw_init(unsigned short serper)
{
    custom.serper = serper;
}

int serial_hw_tx_ready(void)
{
    return (SERDATR_STATUS & STATF_TBE) != 0;
}

int serial_hw_tx_drained(void)
{
    return (SERDATR_STATUS & STATF_TSRE) != 0;
}

void serial_hw_tx(unsigned char c)
{
    /* Bit 8 is the stop bit: Paula sends 9 bits and the ninth must be high. */
    custom.serdat = (unsigned short)c | 0x100;
}

int serial_hw_rx_ready(void)
{
    return (SERDATR_STATUS & STATF_RBF) != 0;
}

/* A word read: this is the access that takes the byte. */
unsigned char serial_hw_rx(void)
{
    return (unsigned char)(custom.serdatr & 0xFF);
}
