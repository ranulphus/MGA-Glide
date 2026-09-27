/* hello.c - harness self-test: PASS normally, FAIL with --fail, never
 * finishes with --hang, faults with --crash. */
#include "hx.h"
#include <stddef.h>

/* Loading an invalid selector into ES raises #GP under any DPMI host. */
#if defined(__WATCOMC__)
void load_bad_selector(void);
#pragma aux load_bad_selector = \
    "mov ax, 0x1235" \
    "mov es, ax" \
    modify [eax];
#else
static void load_bad_selector(void)
{
    __asm__ volatile("movw $0x1235, %%ax\n\tmovw %%ax, %%es" ::: "eax");
}
#endif

int main(int argc, char **argv)
{
    hx_init(argc, argv, "hello");
    hx_test("serial", 1, "COM1 reporting works");
    if (hx_args.fail)
        hx_test("forced", 0, "--fail given");
    if (hx_args.crash)
        load_bad_selector();
    if (hx_args.hang)
        for (;;)
            ;
    hx_snap_screen("hello");
    hx_done(0);
    return 0;
}
