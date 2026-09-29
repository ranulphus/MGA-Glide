/* stackpg - an emulator check for Loop A: a DPMI call must return with the
 * stack pointer it was made with when its interrupt frame straddles into a
 * stack page the program has never touched (CWSDPMI commits it on the page
 * fault and the INT restarts). Unpatched 86Box left ESP 4 or 8 bytes low
 * (local patch 0103), which DJGPP's __dpmi_int then popped into SS: a #GP in
 * any program whose stack happened to meet a fresh page there.
 *
 * Each case moves ESP to fresh_page + k inside an untouched malloc block,
 * issues one INT 31h, and compares ESP after with ESP before; the same call
 * repeated on the now-touched page is the control. Built with DJGPP (the
 * extender under test is CWSDPMI). */
#include "hx.h"
#include <dpmi.h>
#include <go32.h>
#include <stdlib.h>
#include <string.h>

__dpmi_regs sp_rr;
unsigned long sp_before, sp_after, sp_saved, sp_target, sp_bx;
unsigned short sp_ax;

static void call_at(unsigned long esp)
{
    sp_target = esp;
    __asm__ __volatile__(
        "pushl %%ebp\n\t"
        "movl %%esp, _sp_saved\n\t"
        "movl _sp_target, %%esp\n\t"
        "movl %%esp, _sp_before\n\t"
        "movw _sp_ax, %%ax\n\t"
        "movl _sp_bx, %%ebx\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "movl $_sp_rr, %%edi\n\t"
        "int $0x31\n\t"
        "movl %%esp, _sp_after\n\t"
        "movl _sp_saved, %%esp\n\t"
        "popl %%ebp\n\t"
        : : : "eax", "ebx", "ecx", "edx", "esi", "edi", "memory", "cc");
}

/* 0400h stays in protected mode; 0300h runs INT 10h in real mode on the
 * host's stack (SS:SP 0, as __dpmi_int does) or on one in the transfer
 * buffer. */
static const char *what[] = { "0400", "0300-int10-0F", "0300-int10-4F01-tbstack", "0300-int10-4F01" };

int main(int argc, char **argv)
{
    unsigned char *blk;
    unsigned long page;
    int w, k, again, bad = 0, calls = 0;
    hx_init(argc, argv, "stackpg");
    blk = (unsigned char *)malloc(1u << 20);        /* never written: its pages stay untouched */
    if (!blk) {
        hx_test("alloc", 0, "no memory");
        hx_done(1);
    }
    page = ((unsigned long)blk + 65536ul) & ~4095ul;
    for (w = 0; w < 4; w++)
        for (k = -16; k <= 40; k += 4) {
            for (again = 0; again < 2; again++) {
                memset(&sp_rr, 0, sizeof sp_rr);
                sp_bx = 0x10;
                sp_ax = w == 0 ? 0x0400 : 0x0300;
                if (w == 1)
                    sp_rr.x.ax = 0x0F00;
                if (w >= 2) {
                    sp_rr.x.ax = 0x4F01;
                    sp_rr.x.cx = 0x111;
                    sp_rr.x.es = (unsigned short)(__tb >> 4);
                    sp_rr.x.di = (unsigned short)(__tb & 15);
                }
                if (w == 2) {
                    sp_rr.x.ss = (unsigned short)(__tb >> 4);
                    sp_rr.x.sp = (unsigned short)((__tb & 15) + 4096);
                }
                call_at(page + 4096 + (unsigned long)(long)k);
                calls++;
                if (sp_after != sp_before && bad++ < 16)
                    hx_log("HX-STAT stackpg %s k=%d %s esp=%08lx after=%08lx", what[w], k,
                           again ? "touched" : "fresh", sp_before, sp_after);
            }
            page += 8192;                               /* the next case gets untouched pages */
        }
    hx_test("esp-kept", bad == 0, "%d of %d DPMI calls returned a different ESP", bad, calls);
    hx_done(bad ? 1 : 0);
    return 0;
}
