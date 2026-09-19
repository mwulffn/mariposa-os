/*
 * serial_hw.c - Paula UART register primitives
 *
 * See serial_hw.h for why this is the only part of serial that ROM and
 * kernel share.
 */
#include "serial_hw.h"
#include "amiga_hw.h"

void serial_hw_init(unsigned short serper)
{
    custom.serper = serper;
}

int serial_hw_tx_ready(void)
{
    return (custom.serdatr & SERDATF_TBE) != 0;
}

int serial_hw_tx_drained(void)
{
    return (custom.serdatr & SERDATF_TSRE) != 0;
}

void serial_hw_tx(unsigned char c)
{
    /* Bit 8 is the stop bit: Paula sends 9 bits and the ninth must be high. */
    custom.serdat = (unsigned short)c | 0x100;
}

int serial_hw_rx_ready(void)
{
    return (custom.serdatr & SERDATF_RBF) != 0;
}

unsigned char serial_hw_rx(void)
{
    return (unsigned char)(custom.serdatr & 0xFF);
}
