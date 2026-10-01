/* refrast.c - see refrast.h. Implements the HAL's host MMIO hooks. */
#include "refrast.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include <stdlib.h>
#include <string.h>

refrast_t *rr;
void (*refrast_write_hook)(uint32_t off, uint32_t v);
static int8_t dith5[256][2][2], dith6[256][2][2];

void refrast_init(void)
{
    int c;
    if (!rr)
        rr = (refrast_t *)calloc(1, sizeof *rr);
    else
        memset(rr, 0, sizeof *rr);
    for (c = 0; c < 256; c++) {
        int v;
        dith5[c][0][0] = (int8_t)(c >> 3);
        v = (c + 2) >> 3; dith5[c][1][1] = (int8_t)(v > 31 ? 31 : v);
        v = (c + 4) >> 3; dith5[c][1][0] = (int8_t)(v > 31 ? 31 : v);
        v = (c + 6) >> 3; dith5[c][0][1] = (int8_t)(v > 31 ? 31 : v);
        dith6[c][0][0] = (int8_t)(c >> 2);
        v = (c + 1) >> 2; dith6[c][1][1] = (int8_t)(v > 63 ? 63 : v);
        v = (c + 2) >> 2; dith6[c][1][0] = (int8_t)(v > 63 ? 63 : v);
        v = (c + 3) >> 2; dith6[c][0][1] = (int8_t)(v > 63 ? 63 : v);
    }
}

#define REG(o) rr->reg[(o) >> 2]

uint16_t refrast_px16(int x, int y, uint32_t pitch, uint32_t origin_px)
{
    uint32_t i = (uint32_t)y * pitch + origin_px + (uint32_t)x;
    return ((uint16_t *)rr->vram)[i & (RR_VRAM / 2 - 1)];
}

static uint16_t dither16(uint32_t maccess, int r, int g, int b, int x, int y)
{
    int mode = (int)(maccess >> 30);
    switch (mode) {
    case 1: return (uint16_t)((b >> 3) | ((g >> 2) << 5) | ((r >> 3) << 11));       /* no dither 565 */
    case 3: return (uint16_t)((b >> 3) | ((g >> 3) << 5) | ((r >> 3) << 10));       /* no dither 555 */
    case 2: return (uint16_t)(dith5[b][y][x] | (dith5[g][y][x] << 5) | (dith5[r][y][x] << 10));
    default: return (uint16_t)(dith5[b][y][x] | (dith6[g][y][x] << 5) | (dith5[r][y][x] << 11));
    }
}

static int zpass(uint32_t z, uint32_t old, uint32_t mode)
{
    switch (mode) {
    case DWG_ZMODE_ZE: return z == old;
    case DWG_ZMODE_ZNE: return z != old;
    case DWG_ZMODE_ZLT: return z < old;
    case DWG_ZMODE_ZLTE: return z <= old;
    case DWG_ZMODE_ZGT: return z > old;
    case DWG_ZMODE_ZGTE: return z >= old;
    default: return 1;
    }
}

static void edge_step(void)
{
    uint32_t sgn = REG(MGAREG_SGN);
    while (rr->ar[1] < 0 && rr->ar[0]) { rr->ar[1] += rr->ar[0]; rr->fxleft += (sgn & SGN_SDXL) ? -1 : 1; }
    rr->ar[1] += rr->ar[2];
    while (rr->ar[4] < 0 && rr->ar[6]) { rr->ar[4] += rr->ar[6]; rr->fxright += (sgn & SGN_SDXR) ? -1 : 1; }
    rr->ar[4] += rr->ar[5];
}

static void trap(uint32_t ydstlen)
{
    uint32_t dwg = REG(MGAREG_DWGCTL), maccess = REG(MGAREG_MACCESS);
    uint32_t atype = dwg & (7u << 4), zmode = dwg & (7u << 8);
    uint32_t pitch = REG(MGAREG_PITCH) & 0xFE0, ydstorg = REG(MGAREG_YDSTORG);
    int32_t ydst = (int16_t)(ydstlen >> 16), len = (int32_t)(ydstlen & 0xFFFF), row;
    uint32_t cx = REG(MGAREG_CXBNDRY);
    int cxl = (int)(cx & 0xFFF), cxr = (int)((cx >> 16) & 0xFFF);
    uint32_t ytop = REG(MGAREG_YTOP), ybot = REG(MGAREG_YBOT);
    int zw32 = (maccess & MACCESS_ZW32) != 0;
    int32_t ylin = ydst * (int32_t)pitch + (int32_t)ydstorg;
    int sel = ydst & 7;

    rr->draws++;
    for (row = 0; row < len; row++) {
        int16_t xl = rr->fxleft, xr = rr->fxright, oldxl = xl;
        uint32_t zb = rr->dr[0], rb = rr->dr[4], gb = rr->dr[8], bb = rr->dr[12];
        uint64_t zb32 = rr->dr_ext[0];
        int dx;
        while (xl != xr) {
            if (xl >= cxl && xl <= cxr && (uint32_t)ylin >= ytop && (uint32_t)ylin <= ybot) {
                if ((dwg & DWG_SOLID) || atype == DWG_ATYPE_RSTR || atype == DWG_ATYPE_RPL || atype == DWG_ATYPE_BLK) {
                    uint32_t fcol = REG(MGAREG_FCOL);
                    if ((maccess & 3) == MACCESS_PW32)
                        ((uint32_t *)rr->vram)[(uint32_t)(ylin + xl) & (RR_VRAM / 4 - 1)] = fcol;
                    else
                        ((uint16_t *)rr->vram)[(uint32_t)(ylin + xl) & (RR_VRAM / 2 - 1)] = (uint16_t)fcol;
                    rr->pixels++;
                } else {
                    uint32_t zorg = REG(MGAREG_ZORG);
                    uint8_t *zp = rr->vram + (((uint32_t)(ylin + xl) * (zw32 ? 4u : 2u) + zorg) & (RR_VRAM - 1));
                    uint32_t z, old;
                    int pass;
                    if (zw32) {
                        z = (rr->dr_ext[0] & (1ull << 47)) ? 0 : (uint32_t)(rr->dr_ext[0] >> 15);
                        memcpy(&old, zp, 4);
                    } else {
                        z = ((int32_t)rr->dr[0] < 0) ? 0 : (rr->dr[0] >> 15) & 0xFFFF;
                        old = zp[0] | (zp[1] << 8);
                    }
                    pass = zpass(z, old, zmode);
                    if (pass) {
                        int r = (rr->dr[4] & (1u << 23)) ? 0 : (int)((rr->dr[4] >> 15) & 0xFF);
                        int g = (rr->dr[8] & (1u << 23)) ? 0 : (int)((rr->dr[8] >> 15) & 0xFF);
                        int b = (rr->dr[12] & (1u << 23)) ? 0 : (int)((rr->dr[12] >> 15) & 0xFF);
                        if (atype == DWG_ATYPE_ZI) {
                            if (zw32) memcpy(zp, &z, 4);
                            else { zp[0] = (uint8_t)z; zp[1] = (uint8_t)(z >> 8); }
                        }
                        if ((maccess & 3) == MACCESS_PW32)
                            ((uint32_t *)rr->vram)[(uint32_t)(ylin + xl) & (RR_VRAM / 4 - 1)] =
                                (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
                        else
                            ((uint16_t *)rr->vram)[(uint32_t)(ylin + xl) & (RR_VRAM / 2 - 1)] =
                                dither16(maccess, r, g, b, xl & 1, sel & 1);
                        rr->pixels++;
                    }
                }
            }
            if (zw32) { rr->dr_ext[0] += rr->dr_ext[2]; rr->dr[0] = (uint32_t)(rr->dr_ext[0] >> 16); }
            else rr->dr[0] += rr->dr[2];
            rr->dr[4] += rr->dr[6]; rr->dr[8] += rr->dr[10]; rr->dr[12] += rr->dr[14];
            if (xl > xr) xl--; else xl++;
        }
        if (zw32) { rr->dr_ext[0] = zb32 + rr->dr_ext[3]; rr->dr[0] = (uint32_t)(rr->dr_ext[0] >> 16); }
        else rr->dr[0] = zb + rr->dr[3];
        rr->dr[4] = rb + rr->dr[7]; rr->dr[8] = gb + rr->dr[11]; rr->dr[12] = bb + rr->dr[15];
        edge_step();
        dx = (int16_t)(rr->fxleft - oldxl);
        if (zw32) { rr->dr_ext[0] += (uint64_t)((int64_t)dx * (int64_t)rr->dr_ext[2]); rr->dr[0] = (uint32_t)(rr->dr_ext[0] >> 16); }
        else rr->dr[0] += (uint32_t)(dx * (int32_t)rr->dr[2]);
        rr->dr[4] += (uint32_t)(dx * (int32_t)rr->dr[6]);
        rr->dr[8] += (uint32_t)(dx * (int32_t)rr->dr[10]);
        rr->dr[12] += (uint32_t)(dx * (int32_t)rr->dr[14]);
        ylin += (int32_t)pitch;
        sel = (sel + 1) & 7;
    }
}

static uint64_t sext48(uint64_t v) { return (v & (1ull << 47)) ? (v | 0xFFFF000000000000ull) : v; }

void mga_host_wr32(uint32_t off, uint32_t v)
{
    int exec = 0;
    if (!rr)
        refrast_init();
    if (refrast_write_hook)
        refrast_write_hook(off, v);
    if (off >= 0x1C00 && off < 0x1E00 && (off & 0x100)) {
        exec = 1;
        off &= ~0x100u;
    }
    if (off < 0x4000)
        rr->reg[off >> 2] = v;
    switch (off) {
    case MGAREG_AR0: case MGAREG_AR1: case MGAREG_AR2: case MGAREG_AR3:
    case MGAREG_AR4: case MGAREG_AR5: case MGAREG_AR6:
        rr->ar[(off - MGAREG_AR0) >> 2] = (int32_t)v; break;
    case MGAREG_FXBNDRY: rr->fxleft = (int16_t)(v & 0xFFFF); rr->fxright = (int16_t)(v >> 16); break;
    case MGAREG_DR0: rr->dr[0] = v; rr->dr_ext[0] = (rr->dr_ext[0] & ~0xFFFFull) | ((uint64_t)v << 16); break;
    case MGAREG_DR2: rr->dr[2] = v; rr->dr_ext[2] = (rr->dr_ext[2] & ~0xFFFFull) | ((uint64_t)v << 16); break;
    case MGAREG_DR3: rr->dr[3] = v; rr->dr_ext[3] = (rr->dr_ext[3] & ~0xFFFFull) | ((uint64_t)v << 16); break;
    case MGAREG_DR0_Z32LSB: rr->dr_ext[0] = (rr->dr_ext[0] & ~0xFFFFFFFFull) | v; rr->dr[0] = (uint32_t)(rr->dr_ext[0] >> 16); break;
    case MGAREG_DR0_Z32MSB: rr->dr_ext[0] = sext48((rr->dr_ext[0] & 0xFFFFFFFFull) | ((uint64_t)(v & 0xFFFF) << 32)); rr->dr[0] = (uint32_t)(rr->dr_ext[0] >> 16); break;
    case MGAREG_DR2_Z32LSB: rr->dr_ext[2] = (rr->dr_ext[2] & ~0xFFFFFFFFull) | v; break;
    case MGAREG_DR2_Z32MSB: rr->dr_ext[2] = sext48((rr->dr_ext[2] & 0xFFFFFFFFull) | ((uint64_t)(v & 0xFFFF) << 32)); break;
    case MGAREG_DR3_Z32LSB: rr->dr_ext[3] = (rr->dr_ext[3] & ~0xFFFFFFFFull) | v; break;
    case MGAREG_DR3_Z32MSB: rr->dr_ext[3] = sext48((rr->dr_ext[3] & 0xFFFFFFFFull) | ((uint64_t)(v & 0xFFFF) << 32)); break;
    case MGAREG_DR4: case MGAREG_DR6: case MGAREG_DR7: case MGAREG_DR8: case MGAREG_DR10:
    case MGAREG_DR11: case MGAREG_DR12: case MGAREG_DR14: case MGAREG_DR15:
        rr->dr[(off - MGAREG_DR0) >> 2] = v; break;
    case MGAREG_ALPHASTART: rr->alphastart = v; break;
    case MGAREG_FOGSTART: rr->fogstart = v; break;
    default:
        if (off >= MGAREG_TMR0 && off <= MGAREG_TMR(8))
            rr->tmr[(off - MGAREG_TMR0) >> 2] = v;
        break;
    }
    if (exec && off == MGAREG_YDSTLEN) {
        uint32_t op = REG(MGAREG_DWGCTL) & 0xF;
        if (op == DWG_OPCOD_TRAP)
            trap(v);
        else
            rr->unsupported++;
    }
}

uint32_t mga_host_rd32(uint32_t off)
{
    if (off == MGAREG_FIFOSTATUS) return 64 | (1u << 9);
    if (off == MGAREG_STATUS) {
        static uint32_t reads;
        return (++reads & 8) ? STATUS_VSYNCSTS : 0;   /* a vertical retrace every few reads */
    }
    return rr ? rr->reg[(off & 0x3FFF) >> 2] : 0;
}

void mga_host_wr8(uint32_t off, uint8_t v) { (void)off; (void)v; }
uint8_t mga_host_rd8(uint32_t off)
{
    if (off == MGAREG_FIFOSTATUS) return 64;
    if (off == MGAREG_INSTS1) return 0x08;
    return 0;
}
