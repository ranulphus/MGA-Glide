/* serial.c - polled 16550 UART output. */
#include "mga/serial.h"
#include "mga/sys.h"

static uint16_t ser_base;

void serial_init(uint16_t base, uint32_t baud)
{
    uint16_t div = (uint16_t)(115200u / (baud ? baud : 115200u));
    ser_base = base;
    sys_outb(base + 1, 0x00);              /* no interrupts */
    sys_outb(base + 3, 0x80);              /* DLAB */
    sys_outb(base + 0, (uint8_t)(div & 0xFF));
    sys_outb(base + 1, (uint8_t)(div >> 8));
    sys_outb(base + 3, 0x03);              /* 8N1 */
    sys_outb(base + 2, 0xC7);              /* FIFO on, cleared */
    sys_outb(base + 4, 0x03);              /* DTR, RTS */
}

int serial_enabled(void) { return ser_base != 0; }

void serial_putc(char c)
{
    uint32_t spin = 0;
    if (!ser_base)
        return;
    /* Wait for the transmit holding register; give up after a bounded spin so
     * a missing UART never hangs the caller. */
    while (!(sys_inb(ser_base + 5) & 0x20) && ++spin < 100000u)
        ;
    sys_outb(ser_base, (uint8_t)c);
}

void serial_write(const char *s, size_t n)
{
    while (n--) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s++);
    }
}
