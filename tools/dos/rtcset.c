/* RTCSET - change the RTC's year through the BIOS (INT 1Ah 05h, as DOS's
 * DATE does) and report the clock's registers on COM1 before and after:
 *     HX-RTC before a=<hex> b=<hex> year=<hex> cent=<hex>
 *     HX-RTC after a=<hex> b=<hex> year=<hex> cent=<hex> set=<hex>
 * The date set is 2026-10-18, or 1999-10-18 if the century (register 32h) is
 * already 20: a change of century, which a BIOS that keeps the century itself
 * acts on. The bf6's (an Award BIOS on a PIIX4) traps port 71h into SMM and
 * works through the 74h/75h alias; without local patch 0118 every register
 * read as the century afterwards. */
#include <conio.h>
#include <dos.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void puts_(const char *s) { while (*s) put(*s++); }

static void hex(unsigned v)
{
    static const char h[] = "0123456789abcdef";
    put(h[(v >> 4) & 15]);
    put(h[v & 15]);
}

static unsigned char rd(unsigned char r)
{
    unsigned char v;
    _disable();
    outp(0x70, r);
    v = (unsigned char)inp(0x71);
    _enable();
    return v;
}

static void regs(const char *when)
{
    puts_("HX-RTC ");
    puts_(when);
    puts_(" a="), hex(rd(0x0A) & 0x7F);         /* (bit 7, update in progress, comes and goes) */
    puts_(" b="), hex(rd(0x0B));
    puts_(" year="), hex(rd(0x09));
    puts_(" cent="), hex(rd(0x32));
}

int main(void)
{
    union REGS r;
    unsigned char cent = rd(0x32) == 0x20 ? 0x19 : 0x20, year = cent == 0x20 ? 0x26 : 0x99;
    regs("before");
    puts_("\r\n");
    r.h.ah = 0x05;
    r.h.ch = cent;
    r.h.cl = year;
    r.x.dx = 0x1018;
    int86(0x1A, &r, &r);
    regs("after");
    puts_(" set="), hex(year);
    puts_("\r\n");
    return 0;
}
