/* fpcheck - the float-to-integer helpers (mga/fp.h) and the setup
 * conversions (mga/setupconv.h) on the DOS compilers, in 86Box or on a
 * bench PC: tests/unit/fpcheck.inc. Built with Open Watcom (its #pragma aux
 * FISTP forms are checked only here) and with DJGPP. */
#include "hx.h"
#include <stdint.h>

static unsigned long fails;
static int fpc_pass, fpc_86box;
static void fpc_fail(const char *what, double v, long long got, long long want)
{
    static unsigned long per_pass[12];
    if (fpc_pass >= 0 && fpc_pass < 12 && per_pass[fpc_pass]++ < 2)
        hx_log("FPCHECK pass %d (precision %d, mode %d): %s(%.17g): got %08lx%08lx, want %08lx%08lx", fpc_pass,
               fpc_pass / 4, fpc_pass % 4, what, v, (unsigned long)((unsigned long long)got >> 32),
               (unsigned long)((unsigned long long)got & 0xFFFFFFFFu), (unsigned long)((unsigned long long)want >> 32),
               (unsigned long)((unsigned long long)want & 0xFFFFFFFFu));
    fails++;
}

#include "../unit/fpcheck.inc"

int main(int argc, char **argv)
{
    long n;
    hx_init(argc, argv, "FPCHECK");
    fpc_86box = hx_ut_present();
    n = fpcheck_run();
    hx_test("fpcheck", fails == 0, "%ld cases, %lu failures, %ld 86Box FISTP quirks", n, fails, fpc_quirks);
    hx_done(fails ? HX_FAIL : HX_PASS);
    return 0;
}
