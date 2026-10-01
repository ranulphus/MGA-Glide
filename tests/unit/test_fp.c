/* The float-to-integer helpers (mga/fp.h) and the setup conversions
 * (mga/setupconv.h) on the host: tests/unit/fpcheck.inc. In the 32-bit x87
 * build (tests-host's host32 set) the FISTP forms in every rounding mode
 * and precision; in the 64-bit build the portable forms. The Watcom forms
 * are checked in 86Box by tests/hal/fpcheck.c. */
#include "unit.h"
#include <stdint.h>

static int shown, fpc_pass, fpc_86box;
static void fpc_fail(const char *what, double v, long long got, long long want)
{
    unit_checks++;
    unit_failures++;
    if (shown++ < 20)
        fprintf(stderr, "fpcheck: pass %d: %s(%.17g): got %lld, want %lld\n", fpc_pass, what, v, got, want);
}

#include "fpcheck.inc"

int unit_main(void)
{
    long n = fpcheck_run();
    unit_checks++;
    printf("fpcheck: %ld cases%s\n", n,
#ifdef FPC_TAGS
           " (x87: every rounding mode, 24/53/64-bit precision)"
#else
           " (portable forms)"
#endif
    );
    return 0;
}
