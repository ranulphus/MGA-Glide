/* dac.c - RAMDAC gamma ramp. In direct-colour modes the pixel components
 * index the palette RAM, so a ramp there applies display gamma without
 * touching the framebuffer (as the Voodoo's gamma does). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

void dac_set_ramp(const uint8_t ramp[256])
{
    int i;
    MGA_WR8(MGAREG_PALWTADD, 0);
    for (i = 0; i < 256; i++) {
        MGA_WR8(MGAREG_PALDATA, ramp[i]);
        MGA_WR8(MGAREG_PALDATA, ramp[i]);
        MGA_WR8(MGAREG_PALDATA, ramp[i]);
    }
}
