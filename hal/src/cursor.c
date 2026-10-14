/* cursor.c - the hardware cursor of the chips' own DAC, and the display
 * start through the CRTC (docs/matrox-2d.md). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/sys.h"

static void xreg(uint8_t i, uint8_t v)
{
    MGA_WR8(MGAREG_PALWTADD, i);
    MGA_WR8(MGAREG_X_DATAREG, v);
}

/* The image is 64 lines of two 64-bit slices, plane 0 then plane 1, each
 * with its first pixel in bit 63: an AND/XOR row's 8 bytes in reverse. In
 * XGA mode plane 1 is the AND mask and plane 0 the XOR mask: 00 colour 0,
 * 01 colour 1, 10 the screen, 11 the screen inverted. */
int cursor_load(uint32_t off, const uint8_t and_mask[512], const uint8_t xor_mask[512])
{
    uint32_t reach = mga.family == MGA_FAMILY_G100 ? 8u << 20 : 16u << 20;
    int row, i;
    if ((off & 1023) || off + 1024 > reach || (mga.vram_bytes && off + 1024 > mga.vram_bytes))
        return -1;
    for (row = 0; row < 64; row++)
        for (i = 0; i < 8; i++) {
            mga_fb[off + (uint32_t)row * 16 + 7 - (uint32_t)i] = xor_mask[row * 8 + i];
            mga_fb[off + (uint32_t)row * 16 + 15 - (uint32_t)i] = and_mask[row * 8 + i];
        }
    xreg(XREG_XCURADDL, (uint8_t)(off >> 10));
    xreg(XREG_XCURADDH, (uint8_t)(off >> 18));
    return 0;
}

void cursor_colours(uint32_t c0, uint32_t c1)
{
    int i;
    for (i = 0; i < 3; i++) {
        xreg((uint8_t)(XREG_XCURCOL0R + i), (uint8_t)(c0 >> (16 - 8 * i)));
        xreg((uint8_t)(XREG_XCURCOL1R + i), (uint8_t)(c1 >> (16 - 8 * i)));
    }
}

void cursor_show(int on)
{
    xreg(XREG_XCURCTRL, on ? XCURCTRL_XGA : XCURCTRL_OFF);
}

/* The registers hold the bottom right corner plus one (0: off the screen);
 * a new position counts from the next vertical retrace, and must not be
 * written during one. */
void cursor_move(int x, int y)
{
    int px = x + 64, py = y + 64;
    uint32_t t0 = sys_time_us();
    if (px < 1 || py < 1 || px > 4095 || py > 4095)
        px = py = 0;
    while ((MGA_RD32(MGAREG_STATUS) & STATUS_VSYNCSTS) && (uint32_t)(sys_time_us() - t0) < 2000u)
        ;
    MGA_WR8(MGAREG_CURPOSXL, (uint8_t)px);
    MGA_WR8(MGAREG_CURPOSXL + 1, (uint8_t)(px >> 8));
    MGA_WR8(MGAREG_CURPOSXL + 2, (uint8_t)py);
    MGA_WR8(MGAREG_CURPOSXL + 3, (uint8_t)(py >> 8));
}

/* startadd counts 8 bytes at 8, 16 and 32 bits a pixel; CRTCEXT0 goes last,
 * and the new start holds from the next line on (between frames if called
 * in the vertical blank). */
int crtc_set_start(uint32_t byte_off)
{
    uint32_t a = byte_off >> 3;
    uint8_t e;
    if ((byte_off & 7) || a >= (1u << 21))
        return -1;
    MGA_WR8(MGAREG_CRTC_INDEX, CRTC_STARTADD_HI);
    MGA_WR8(MGAREG_CRTC_DATA, (uint8_t)(a >> 8));
    MGA_WR8(MGAREG_CRTC_INDEX, CRTC_STARTADD_LO);
    MGA_WR8(MGAREG_CRTC_DATA, (uint8_t)a);
    MGA_WR8(MGAREG_CRTCEXT_INDEX, CRTCEXT0);
    e = MGA_RD8(MGAREG_CRTCEXT_DATA);
    MGA_WR8(MGAREG_CRTCEXT_DATA, (uint8_t)((e & 0xB0) | ((a >> 16) & 0x0F) | ((a >> 14) & 0x40)));
    return 0;
}
