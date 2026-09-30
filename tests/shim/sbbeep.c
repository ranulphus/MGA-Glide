/* sbbeep - a Loop A check of --sound and --wav: plays a 440 Hz tone for two
 * seconds through the Sound Blaster's DAC in direct mode (DSP command 10h,
 * one 8-bit sample per write, paced with uclock), so it needs no DMA or IRQ.
 * make loopa-selftest runs it with --sound sb16 --wav and looks for 440 Hz
 * in the recording (tools/loopa/wavcheck.py).
 *
 * The port comes from BLASTER's A field (default 220). Built with DJGPP. */
#include "hx.h"
#include <math.h>
#include <pc.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int base = 0x220;

static int dsp_write(int v)
{
    long spin = 0;
    while ((inportb(base + 0xC) & 0x80) && ++spin < 100000L) ;
    outportb(base + 0xC, v);
    return spin < 100000L;
}

static int dsp_reset(void)
{
    uclock_t t;
    outportb(base + 0x6, 1);
    t = uclock() + UCLOCKS_PER_SEC / 10000 + 1;   /* at least 3 us */
    while (uclock() < t) ;
    outportb(base + 0x6, 0);
    t = uclock() + UCLOCKS_PER_SEC / 10;
    while (uclock() < t)
        if ((inportb(base + 0xE) & 0x80) && inportb(base + 0xA) == 0xAA)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *b = getenv("BLASTER"), *a;
    const long rate = 11025;
    uclock_t start, due;
    long n, total = 2 * rate, late = 0;

    hx_init(argc, argv, "sbbeep");
    if (b && (a = strchr(b, 'A')) != NULL)
        base = (int)strtol(a + 1, NULL, 16);
    hx_test("reset", dsp_reset(), "DSP at %x answers AAh", base);
    if (hx_failures())
        hx_done(1);
    dsp_write(0xD1);                            /* speaker on */
    start = uclock();
    for (n = 0; n < total; n++) {
        int s = 128 + (int)(100.0 * sin(2.0 * M_PI * 440.0 * (double)n / (double)rate));
        due = start + (uclock_t)((double)n * UCLOCKS_PER_SEC / rate);
        while (uclock() < due) ;
        if (uclock() - due > UCLOCKS_PER_SEC / 1000)
            late++;
        dsp_write(0x10);
        dsp_write(s);
    }
    dsp_write(0xD3);                            /* speaker off */
    hx_stat("sbbeep samples=%ld rate=%ld late=%ld", total, rate, late);
    hx_test("played", 1, "%ld samples at %ld Hz", total, rate);
    hx_done(0);
    return 0;
}
