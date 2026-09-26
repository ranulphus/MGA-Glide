/* fifo.c - FIFO pacing: every burst of register writes reserves its slots
 * here first, against the depth the chip reports (PRD R3). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

static int fifo_free;

void fifo_reset(void) { fifo_free = 0; }

void fifo_reserve(int n)
{
    uint32_t spin = 0;
    while (fifo_free < n) {
        fifo_free = MGA_RD8(MGAREG_FIFOSTATUS) & mga.fifo_mask;
        if (++spin > 20000000u) {        /* engine wedged: let the caller notice via sync */
            fifo_free = n;
            break;
        }
    }
    fifo_free -= n;
}
