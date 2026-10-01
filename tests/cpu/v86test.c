/* V86TEST [path\V86PM.BIN] - CPU self-test for the paths a virtual-8086
 * monitor depends on (GLOS, PRD H5). Real-mode cases run here; the rest run
 * in a 32-bit monitor (pm_main.c, loaded from V86PM.BIN) that this program
 * enters with paging on and returns from. Results are HX-TEST lines on COM1:
 *     HX-TEST cpu-<case>-<name> PASS|FAIL|INFO <detail>
 * then HX-DONE 0 when nothing failed, 1 otherwise. The harness
 * (tools/loopa/selftest_ext.py) accepts the failures listed for 86Box in
 * tests/cpu/known-86box.txt. 16-bit, Open Watcom small model. */
#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "v86pm.h"

#ifndef HX_BUILD_ID
#define HX_BUILD_ID "unknown"
#endif

extern unsigned long __cdecl pm_call(unsigned gdtr, unsigned long cr3, unsigned long entry, unsigned long arg);
extern void __cdecl pm_ret(void);

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void say(const char *s) { while (*s) put(*s++); }

static void sayu(unsigned long v)
{
    char b[12];
    int i = 0;
    do b[i++] = (char)('0' + v % 10); while (v /= 10);
    while (i) put(b[--i]);
}

static void sayx(unsigned long v, int digits)
{
    static const char h[] = "0123456789abcdef";
    while (digits--) put(h[(v >> (digits * 4)) & 15]);
}

static unsigned fails;

static void result(const char *name, int ok, const char *detail)
{
    say("HX-TEST cpu-");
    say(name);
    say(ok > 0 ? " PASS " : ok == 0 ? " FAIL " : " INFO ");
    say(detail);
    say("\r\n");
    if (ok == 0) fails++;
}

/* ---- R: does the BIOS's INT 15h AH=86h wait use the RTC periodic interrupt? */

static void (__interrupt __far *old70)(void);
static volatile unsigned r_irq8, r_pie;

static void __interrupt __far hook70(void)
{
    unsigned char b;
    r_irq8++;
    outp(0x70, 0x0B);
    b = (unsigned char)inp(0x71);
    if (b & 0x40) r_pie = 1;
    _chain_intr(old70);
}

static void case_r(void)
{
    union REGS r;
    unsigned char b_before, b_after;
    char d[96];

    outp(0x70, 0x0B);
    b_before = (unsigned char)inp(0x71);
    old70 = _dos_getvect(0x70);
    _dos_setvect(0x70, hook70);
    r.h.ah = 0x86;
    r.x.cx = 0;
    r.x.dx = 50000U;                            /* 50 ms */
    int86(0x15, &r, &r);
    _dos_setvect(0x70, old70);
    outp(0x70, 0x0B);
    b_after = (unsigned char)inp(0x71);
    sprintf(d, "cf=%u irq8=%u pie_during=%u regb_before=%02x regb_after=%02x",
            r.x.cflag ? 1 : 0, r_irq8, r_pie, b_before, b_after);
    result("R-int15-86-rtc", -1, d);
}

/* ---- S: A20 by INT 15h 24xxh, port 92h and the keyboard controller */

/* 1 when FFFF:0510 is a different byte from 0000:0500 (A20 on). */
static int a20_on(void)
{
    unsigned char far *lo = (unsigned char far *)MK_FP(0x0000, 0x0500);
    unsigned char far *hi = (unsigned char far *)MK_FP(0xFFFF, 0x0510);
    unsigned char save_lo = *lo, save_hi = *hi;
    int on;
    *lo = 0x5A;
    *hi = 0xA5;
    on = *lo == 0x5A;
    *hi = save_hi;
    *lo = save_lo;
    return on;
}

static void kbc_wait(void)
{
    long spin = 0;
    while ((inp(0x64) & 2) && ++spin < 100000L) ;
}

static void kbc_a20(int on)
{
    kbc_wait();
    outp(0x64, 0xD1);
    kbc_wait();
    outp(0x60, on ? 0xDF : 0xDD);
    kbc_wait();
}

static void case_s(void)
{
    union REGS r;
    int initial, ok, on1, off1;
    unsigned char p92;
    char d[80];

    _disable();
    initial = a20_on();
    /* INT 15h 2401h/2400h */
    r.x.ax = 0x2401; int86(0x15, &r, &r); on1 = !r.x.cflag && a20_on();
    r.x.ax = 0x2400; int86(0x15, &r, &r); off1 = !r.x.cflag && !a20_on();
    sprintf(d, "on=%d off=%d", on1, off1);
    result("S-a20-int15", (on1 && off1) ? 1 : -1, d);
    /* port 92h bit 1 */
    p92 = (unsigned char)inp(0x92);
    outp(0x92, (p92 | 2) & ~1);  on1 = a20_on();
    outp(0x92, p92 & ~3);        off1 = !a20_on();
    ok = on1 && off1;
    sprintf(d, "on=%d off=%d", on1, off1);
    result("S-a20-port92", ok ? 1 : -1, d);     /* not every board has a fast A20 gate */
    /* keyboard controller command D1h */
    kbc_a20(1); on1 = a20_on();
    kbc_a20(0); off1 = !a20_on();
    sprintf(d, "on=%d off=%d", on1, off1);
    result("S-a20-kbc", (on1 && off1) ? 1 : 0, d);
    /* as found */
    kbc_a20(initial);
    outp(0x92, initial ? ((p92 | 2) & ~1) : (p92 & ~3));
    _enable();
    sprintf(d, "initial=%d restored=%d", initial, a20_on());
    result("S-a20-restore", a20_on() == initial ? 1 : 0, d);
}

/* ---- protected mode */

static unsigned char gdt[8 * 5];
static struct { unsigned short limit; unsigned long base; } gdtr;
static struct bootinfo bi;

static void set_desc(int sel, unsigned long base, unsigned long limit, unsigned char access, unsigned char flags)
{
    unsigned char *e = gdt + sel;
    e[0] = (unsigned char)limit; e[1] = (unsigned char)(limit >> 8);
    e[2] = (unsigned char)base; e[3] = (unsigned char)(base >> 8); e[4] = (unsigned char)(base >> 16);
    e[5] = access;
    e[6] = (unsigned char)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    e[7] = (unsigned char)(base >> 24);
}

static void far *phys(unsigned long p)
{
    return MK_FP((unsigned)(p >> 4), (unsigned)(p & 15));
}

static void poke32(unsigned long p, unsigned long v)
{
    *(unsigned long far *)phys(p) = v;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "C:\\HX\\V86PM.BIN";
    struct SREGS sr;
    unsigned seg, i, pages;
    unsigned long base, a, total, got, ret;
    FILE *f;
    struct { char magic[4]; unsigned long entry, file_size, total; } hdr;
    static unsigned char buf[1024];

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    say("HX-START v86test " HX_BUILD_ID "\r\n");

    case_r();
    case_s();

    f = fopen(path, "rb");
    if (!f || fread(&hdr, sizeof hdr, 1, f) != 1 || memcmp(hdr.magic, "V86P", 4)) {
        result("load", 0, "no V86PM.BIN");
        say("HX-DONE 1\r\n");
        return 1;
    }
    /* 0x7000 bytes of tables and scratch, then the monitor, 4 KB aligned. */
    total = 0x7000UL + ((hdr.total + 0xFFF) & ~0xFFFUL);
    if (_dos_allocmem((unsigned)((total + 0x1000 + 15) >> 4), &seg) != 0) {
        result("load", 0, "not enough conventional memory");
        say("HX-DONE 1\r\n");
        return 1;
    }
    base = ((unsigned long)seg * 16 + 0xFFF) & ~0xFFFUL;
    for (a = 0; a < total; a += 2)
        *(unsigned far *)phys(base + a) = 0;
    bi.pd = base;
    bi.pt0 = base + 0x1000;
    bi.pt1 = base + 0x2000;
    bi.v86_seg = (base + 0x3000) >> 4;
    bi.spare_a = base + 0x5000;
    bi.spare_b = base + 0x6000;
    bi.blob_phys = base + 0x7000;
    pages = (unsigned)((hdr.total + 0xFFF) >> 12);
    bi.blob_pages = pages;
    /* the monitor image (the header is its first 16 bytes) */
    fseek(f, 0, SEEK_SET);
    for (got = 0; got < hdr.file_size; ) {
        unsigned n = (unsigned)(hdr.file_size - got > sizeof buf ? sizeof buf : hdr.file_size - got);
        if (fread(buf, 1, n, f) != n) break;
        _fmemcpy(phys(bi.blob_phys + got), buf, n);
        got += n;
    }
    fclose(f);
    if (got != hdr.file_size) {
        result("load", 0, "short read");
        say("HX-DONE 1\r\n");
        return 1;
    }
    /* Page directory: 0-4 MB and 4-8 MB, user-accessible at this level. */
    poke32(bi.pd, bi.pt0 | 7);
    poke32(bi.pd + 4, bi.pt1 | 7);
    /* 0-10FFFFh identity, user-accessible (V86 code reaches the IVT too). */
    for (i = 0; i < 0x110; i++)
        poke32(bi.pt0 + i * 4UL, ((unsigned long)i << 12) | 7);
    /* The monitor at 4 MB, supervisor-only; it opens its .user pages itself. */
    for (i = 0; i < pages; i++)
        poke32(bi.pt1 + i * 4UL, (bi.blob_phys + ((unsigned long)i << 12)) | 3);

    segread(&sr);
    bi.magic = V86PM_MAGIC;
    bi.cs_base = (unsigned long)sr.cs << 4;
    bi.ds_base = (unsigned long)sr.ds << 4;
    bi.ret_off = (unsigned)pm_ret;
    set_desc(SEL_CODE32, 0, 0xFFFFF, 0x9A, 0xC0);
    set_desc(SEL_DATA32, 0, 0xFFFFF, 0x92, 0xC0);
    set_desc(SEL_CODE16, bi.cs_base, 0xFFFF, 0x9A, 0x00);
    set_desc(SEL_DATA16, bi.ds_base, 0xFFFF, 0x92, 0x00);
    gdtr.limit = sizeof gdt - 1;
    gdtr.base = bi.ds_base + (unsigned)gdt;

    ret = pm_call((unsigned)&gdtr, bi.pd, V86PM_BASE + hdr.entry, bi.ds_base + (unsigned)&bi);
    _dos_freemem(seg);
    fails += (unsigned)ret;
    say("HX-TEST cpu-summary INFO monitor_fails=");
    sayu(ret);
    say(" magic=");
    sayx(bi.magic, 8);
    say("\r\nHX-DONE ");
    say(fails ? "1" : "0");
    say("\r\n");
    return fails ? 1 : 0;
}
