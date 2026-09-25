/* serial.h - unbuffered, polled 16550 logging (PRD §9).
 * Every byte is written synchronously so the last line survives a hang. */
#ifndef MGA_SERIAL_H
#define MGA_SERIAL_H
#include "mga/types.h"

#define SERIAL_COM1 0x3F8

void serial_init(uint16_t base, uint32_t baud);
int  serial_enabled(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
void serial_puts(const char *s);

#endif
