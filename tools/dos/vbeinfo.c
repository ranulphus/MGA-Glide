/* VBEINFO - report the video BIOS's VESA support on COM1 for the harness:
 *     HX-VBE ver=<BCD version or none> mem=<KB> modes=<count> lfb=<n>
 *            m640x480x16=<mode or none> m800x600x16=<...> m1024x768x8=<...>
 * lfb counts the modes with a linear framebuffer (attribute bit 7). The
 * three named modes are the ones GLOS's desktop needs on a 1-4 MB card. */
#include <conio.h>
#include <dos.h>
#include <string.h>

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

static void dec(unsigned long v)
{
    char b[12];
    int i = 0;
    do b[i++] = (char)('0' + v % 10); while (v /= 10);
    while (i) put(b[--i]);
}

static unsigned char info[512];                 /* VbeInfoBlock */
static unsigned char mi[256];                   /* ModeInfoBlock */

/* INT 10h with ES:DI -> buf (in DGROUP). Returns AX. */
static unsigned vbe_call(unsigned ax, unsigned cx, void *buf)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = s.ds;
    r.x.ax = ax;
    r.x.cx = cx;
    r.x.di = (unsigned)buf;
    int86x(0x10, &r, &r, &s);
    return r.x.ax;
}

int main(void)
{
    unsigned short far *list;
    unsigned ver, n = 0, lfb = 0, mode;
    unsigned want[3] = { 0xFFFF, 0xFFFF, 0xFFFF };
    static const unsigned ww[3] = { 640, 800, 1024 }, wh[3] = { 480, 600, 768 };
    static const unsigned char wb[3] = { 16, 16, 8 };
    int i;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    memcpy(info, "VBE2", 4);
    if (vbe_call(0x4F00, 0, info) != 0x004F || memcmp(info, "VESA", 4)) {
        puts_("HX-VBE ver=none\r\n");
        return 0;
    }
    ver = info[4] | (info[5] << 8);
    list = (unsigned short far *)MK_FP(*(unsigned *)(info + 16), *(unsigned *)(info + 14));
    while ((mode = *list++) != 0xFFFF && n < 512) {
        n++;
        memset(mi, 0, sizeof mi);
        if (vbe_call(0x4F01, mode, mi) != 0x004F)
            continue;
        if (mi[0] & 0x80) {                     /* linear framebuffer available */
            unsigned w = mi[18] | (mi[19] << 8), h = mi[20] | (mi[21] << 8);
            unsigned char bpp = mi[25];
            lfb++;
            for (i = 0; i < 3; i++)
                if (want[i] == 0xFFFF && w == ww[i] && h == wh[i] && bpp == wb[i])
                    want[i] = mode;
        }
    }
    puts_("HX-VBE ver=");
    hex(ver, 4);
    puts_(" mem=");
    dec((unsigned long)(info[18] | (info[19] << 8)) * 64);
    puts_(" modes=");
    dec(n);
    puts_(" lfb=");
    dec(lfb);
    for (i = 0; i < 3; i++) {
        puts_(i == 0 ? " m640x480x16=" : i == 1 ? " m800x600x16=" : " m1024x768x8=");
        if (want[i] == 0xFFFF) puts_("none"); else hex(want[i], 3);
    }
    puts_("\r\n");
    return 0;
}
