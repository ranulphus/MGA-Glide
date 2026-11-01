/* clockmon.c - the DJGPP port's sys_time_us() never goes back (CLOCKMON [S]).
 *
 * uclock() reads a tick (54.9 ms) behind when the PIT has wrapped but IRQ 0
 * hasn't yet counted it at 0x46C: 3 to 5 times a minute of tight reading in
 * 86Box, and a HAL wait's (uint32_t)(now - start) then reads as 71 minutes.
 * S seconds (60) by the BIOS tick of raw uclock(), then S of sys_time_us(),
 * each read alone as a wait reads it:
 *   HX-STAT clock uclock_reads=N uclock_backs=K reads=N backs=M held=C
 *   HX-TEST clock PASS|FAIL   (sys_time_us never below its last reading)
 * held is the port's sys_time_backs: the steps back it held the clock over.
 * A line every 20 s keeps the run from looking idle. */
#include "hx.h"
#include "mga/sys.h"
#include <go32.h>
#include <stdlib.h>
#include <sys/farptr.h>
#include <time.h>

static unsigned long tick(void) { return _farpeekl(_dos_ds, 0x46C); }

int main(int argc, char **argv)
{
    unsigned long t0, t, said, ticks, raw_reads = 0, raw_backs = 0, reads = 0, backs = 0;
    uclock_t raw, raw_last;
    uint32_t us, us_last;
    ticks = (argc > 1 ? atoi(argv[1]) : 60) * 182UL / 10;
    hx_init(1, argv, "clockmon");               /* (S is ours, not the shim's) */
    raw_last = uclock();
    t0 = said = tick();
    do {
        raw = uclock();
        if (raw < raw_last)
            raw_backs++;
        raw_last = raw;
        raw_reads++;
        if ((t = tick()) - said >= 364) {
            hx_log("CLOCKMON uclock %lu s, %lu back", (t - t0) * 10 / 182, raw_backs);
            said = t;
        }
    } while (t - t0 < ticks);
    us_last = sys_time_us();
    t0 = said = tick();
    do {
        us = sys_time_us();
        if ((int32_t)(us - us_last) < 0)
            backs++;
        us_last = us;
        reads++;
        if ((t = tick()) - said >= 364) {
            hx_log("CLOCKMON sys_time_us %lu s, %lu back", (t - t0) * 10 / 182, backs);
            said = t;
        }
    } while (t - t0 < ticks);
    hx_stat("clock uclock_reads=%lu uclock_backs=%lu reads=%lu backs=%lu held=%lu", raw_reads, raw_backs, reads,
            backs, (unsigned long)sys_time_backs);
    hx_test("clock", backs == 0, "%lu of %lu readings went back (uclock alone: %lu of %lu)", backs, reads, raw_backs,
            raw_reads);
    hx_done(hx_failures() ? HX_FAIL : HX_PASS);
}
