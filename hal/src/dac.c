/* dac.c - RAMDAC gamma ramp. In direct-colour modes the pixel components
 * index the palette RAM, so a ramp there applies display gamma without
 * touching the framebuffer (as the Voodoo's gamma does).
 *
 * Each component indexes with its own value: in 16-bit modes red and blue
 * (0-31) pick entries 0-31 and green (0-63) entries 0-63, in 15-bit modes
 * all three 0-31, in 24- and 32-bit modes the 8-bit value. That is how the
 * video BIOS loads the palette (entry i = i*8, i*4, i*8 after a 16-bit mode
 * set) and how a G400 reads it (bench-g400, 2026-10-26: a ramp indexed by
 * the 8-bit value drew every 16-bit frame at an eighth of its brightness). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

#define XMULCTRL 0x19

/* The 8-bit input whose output entry i holds, for a component of `bits` bits. */
static int expand(int i, int bits)
{
    if (bits == 5) {
        i &= 31;
        return (i << 3) | (i >> 2);
    }
    if (bits == 6) {
        i &= 63;
        return (i << 2) | (i >> 4);
    }
    return i;
}

void dac_set_ramp(const uint8_t ramp[256])
{
    int depth, rb, g, i;
    MGA_WR8(MGAREG_PALWTADD, XMULCTRL);
    depth = MGA_RD8(MGAREG_X_DATAREG) & 7;      /* 1: 15-bit, 2: 16-bit, else 8 bits a component */
    rb = depth == 1 || depth == 2 ? 5 : 8;
    g = depth == 1 ? 5 : depth == 2 ? 6 : 8;
    MGA_WR8(MGAREG_PALWTADD, 0);
    for (i = 0; i < 256; i++) {
        MGA_WR8(MGAREG_PALDATA, ramp[expand(i, rb)]);
        MGA_WR8(MGAREG_PALDATA, ramp[expand(i, g)]);
        MGA_WR8(MGAREG_PALDATA, ramp[expand(i, rb)]);
    }
}
