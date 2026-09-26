/* refrast.h - host model of the MGA drawing engine (G100 behaviour as
 * modelled by 86Box), fed by the HAL's MGA_WR32 on host builds. Used by
 * unit tests and the host trace replayer. */
#ifndef REFRAST_H
#define REFRAST_H
#include <stdint.h>

#define RR_VRAM (16u << 20)

typedef struct {
    uint8_t  vram[RR_VRAM];
    uint32_t reg[0x4000 / 4];     /* last written value of each register */
    uint32_t dr[16];
    uint64_t dr_ext[4];           /* 48-bit Z accumulators (DR0, DR2, DR3) */
    int32_t  ar[7];
    int16_t  fxleft, fxright;
    uint32_t alphastart, fogstart;
    uint32_t tmr[9];
    uint32_t draws, pixels;
    uint32_t unsupported;
} refrast_t;

extern refrast_t *rr;
void refrast_init(void);
uint16_t refrast_px16(int x, int y, uint32_t pitch, uint32_t origin_px);

#endif
