/*
 * serial.c - Serial port output (polling)
 *
 * Uses Paula's UART at $DFF000.
 * 9600 baud, 8N1.
 */

#include "serial.h"
#include "amiga_hw.h"

void ser_init(void)
{
    /* 9600 baud - ROM should have set this, but be sure */
    custom.serper = SERPER_9600;
}

void ser_putc(char c)
{
    /* Wait for transmit buffer empty */
    while (!(custom.serdatr & SERDATF_TBE))
        ;
    
    /* Send character with stop bit */
    custom.serdat = (unsigned short)c | 0x100;
}

void ser_puts(const char *s)
{
    while (*s)
        ser_putc(*s++);
}

int ser_can_read(void)
{
    return (custom.serdatr & SERDATF_RBF) != 0;
}

char ser_getc(void)
{
    /* Wait for receive buffer full */
    while (!(custom.serdatr & SERDATF_RBF))
        ;
    
    /* Read data, clear RBF by reading */
    return (char)(custom.serdatr & 0xFF);
}
