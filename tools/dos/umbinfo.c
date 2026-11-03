/* UMBINFO - DOS's upper memory blocks, on COM1 for the harness:
 *     HX-UMB link=<0|1> blocks=<N> free=<bytes> largest=<bytes> first=<SEG|none>
 * link is DOS's UMB link state as found (INT 21h 5802h). The arena chain is
 * walked with the UMBs linked (5803h, put back after): blocks counts the
 * arenas at A000h and above, free and largest the free ones among them,
 * first is the first such arena's segment. With no UMBs, blocks=0. */
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

static void dec(unsigned long v)
{
    char b[12];
    int i = 0;
    do b[i++] = (char)('0' + v % 10); while (v /= 10);
    while (i) put(b[--i]);
}

int main(void)
{
    union REGS r;
    struct SREGS s;
    unsigned link, seg, first = 0, blocks = 0, n = 0;
    unsigned long free_ = 0, largest = 0;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    r.x.ax = 0x5802;
    intdos(&r, &r);
    link = r.x.cflag ? 0 : r.h.al;
    r.x.ax = 0x5803;
    r.x.bx = 1;
    intdos(&r, &r);
    segread(&s);
    r.h.ah = 0x52;                              /* the first arena is the word before ES:BX */
    intdosx(&r, &r, &s);
    seg = *(unsigned far *)MK_FP(s.es, r.x.bx - 2);
    while (++n < 4096) {
        unsigned char far *m = (unsigned char far *)MK_FP(seg, 0);
        unsigned owner = *(unsigned far *)(m + 1), size = *(unsigned far *)(m + 3);
        if (m[0] != 'M' && m[0] != 'Z')
            break;                              /* (a broken chain: what was counted stands) */
        if (seg >= 0xA000) {
            if (!first)
                first = seg;
            blocks++;
            if (!owner) {
                free_ += (unsigned long)size * 16;
                if ((unsigned long)size * 16 > largest)
                    largest = (unsigned long)size * 16;
            }
        }
        if (m[0] == 'Z')
            break;
        seg += size + 1;
    }
    r.x.ax = 0x5803;
    r.x.bx = link;
    intdos(&r, &r);
    puts_("HX-UMB link=");
    dec(link);
    puts_(" blocks=");
    dec(blocks);
    puts_(" free=");
    dec(free_);
    puts_(" largest=");
    dec(largest);
    puts_(" first=");
    if (first)
        hex(first, 4);
    else
        puts_("none");
    puts_("\r\n");
    return 0;
}
