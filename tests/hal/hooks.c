/* hooks.c - the HAL port's fault and exit hooks: normal exit runs the exit
 * hook; --crash sets 640x480x16 and loads a bad selector, and the fault
 * hook must report the exception and put the display back in text mode
 * before the previous handler ends the program. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <stdlib.h>

#if defined(__WATCOMC__)
void load_bad_selector(void);
#pragma aux load_bad_selector = "mov ax, 0x1235" "mov es, ax" modify [eax];
#else
static void load_bad_selector(void)
{
    __asm__ volatile("movw $0x1235, %%ax\n\tmovw %%ax, %%es" ::: "eax");
}
#endif

static void on_fault(int exc, uint32_t err, uint32_t eip)
{
    vbe_set_text_mode();
    hx_log("HX-FAULT exc=%d err=%lx eip=%08lx", exc, (unsigned long)err, (unsigned long)eip);
}

static void on_exit_hook(void)
{
    hx_log("HX-EXITHOOK ran");
}

int main(int argc, char **argv)
{
    mga_vbe_mode m;
    int pitch;
    hx_init(argc, argv, "hooks");
    hx_test("hook-faults", sys_hook_faults(on_fault) == 0, "");
    hx_test("hook-exit", sys_hook_exit(on_exit_hook) == 0, "");
    if (hx_args.crash) {
        if (mga_find(&mga) == 0 && mga_map(&mga) == 0 && vbe_find_mode(640, 480, 16, &m) == 0)
            vbe_set_mode(&m, 1024, &pitch);
        load_bad_selector();
        hx_test("fault", 0, "no fault raised");
    }
    hx_done(0);
    return 0;
}
