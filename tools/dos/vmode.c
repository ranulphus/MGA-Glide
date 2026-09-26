/* VMODE - report the current video mode on COM1 for the harness:
 *     HX-VMODE bios=<int 10h/0Fh mode> vbe=<VBE 4F03h mode or none>
 * Run after a program to check it left the display in text mode. */
#include <conio.h>
#include <dos.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void puts_(const char *s) { while (*s) put(*s++); }

static void hex(unsigned v, int digits)
{
    static const char h[] = "0123456789abcdef";
    while (digits--)
        put(h[(v >> (digits * 4)) & 15]);
}

int main(void)
{
    union REGS r;
    unsigned bios, vbe_ok, vbe;
    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    bios = r.h.al & 0x7F;
    r.x.ax = 0x4F03;
    int86(0x10, &r, &r);
    vbe_ok = r.x.ax == 0x004F;
    vbe = r.x.bx;
    puts_("HX-VMODE bios=");
    hex(bios, 2);
    puts_(" vbe=");
    if (vbe_ok) hex(vbe & 0x3FFF, 4); else puts_("none");
    put('\r'); put('\n');
    return 0;
}
