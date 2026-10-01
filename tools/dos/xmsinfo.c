/* XMSINFO - report the XMS driver and where DOS lives on COM1 for the harness:
 *     HX-XMS ver=<BCD> rev=<BCD> hma=<0|1> free=<KB> largest=<KB> doshigh=<0|1>
 * or "HX-XMS none" with no driver. free and largest come from XMS 08h (at
 * most 64 MB); doshigh is INT 21h 3306h's "DOS in the HMA" bit. */
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

static void dec(unsigned v)
{
    char b[8];
    int i = 0;
    do b[i++] = (char)('0' + v % 10); while (v /= 10);
    while (i) put(b[--i]);
}

static void (far *xms)(void);

/* Call the XMS entry with AH=fn; returns AX, BX and DX. */
static void xms_call(unsigned char fn, unsigned *ax, unsigned *bx, unsigned *dx)
{
    unsigned a, b, d;
    _asm {
        mov ah, fn
        call dword ptr xms
        mov a, ax
        mov b, bx
        mov d, dx
    }
    *ax = a; *bx = b; *dx = d;
}

int main(void)
{
    union REGS r;
    struct SREGS s;
    unsigned ver, rev, hma, largest, total, bx, dx, doshigh;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    r.x.ax = 0x3306;
    intdos(&r, &r);
    doshigh = (r.h.dh & 0x10) ? 1 : 0;
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80) {
        puts_("HX-XMS none doshigh=");
        dec(doshigh);
        puts_("\r\n");
        return 0;
    }
    segread(&s);
    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &s);
    xms = (void (far *)(void))MK_FP(s.es, r.x.bx);
    xms_call(0x00, &ver, &rev, &hma);
    xms_call(0x08, &largest, &bx, &total);
    (void)bx; (void)dx;
    puts_("HX-XMS ver=");
    hex(ver, 4);
    puts_(" rev=");
    hex(rev, 4);
    puts_(" hma=");
    dec(hma ? 1 : 0);
    puts_(" free=");
    dec(total);
    puts_(" largest=");
    dec(largest);
    puts_(" doshigh=");
    dec(doshigh);
    puts_("\r\n");
    return 0;
}
