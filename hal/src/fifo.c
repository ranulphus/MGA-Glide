/* fifo.c - FIFO pacing: every burst of register writes reserves its slots
 * here first, against the depth the chip reports (PRD R3). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/prof.h"

#ifdef MGA_PROF
uint64_t prof_cyc[PROF_N];
uint32_t prof_n[PROF_N];
uint64_t prof_t;
int      prof_cur;
uint32_t prof_wr, prof_fifo_rd;
#endif

int mga_fifo_free;                      /* slots known free (fifo_need's fast path) */

void fifo_reset(void) { mga_fifo_free = 0; }

void fifo_reserve(int n)
{
    uint32_t spin = 0;
#ifdef MGA_PROF
    int o;
    if (mga_fifo_free >= n) {
        mga_fifo_free -= n;
        return;
    }
    o = PROF_SWITCH(PROF_FIFO);
#endif
    while (mga_fifo_free < n) {
        mga_fifo_free = MGA_RD8(MGAREG_FIFOSTATUS) & mga.fifo_mask;
#ifdef MGA_PROF
        prof_fifo_rd++;
#endif
        if (++spin > 20000000u) {        /* engine wedged: let the caller notice via sync */
            mga_fifo_free = n;
            break;
        }
    }
    mga_fifo_free -= n;
#ifdef MGA_PROF
    PROF_BACK(o);
#endif
}
