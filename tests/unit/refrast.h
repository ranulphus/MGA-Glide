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
/* Width of AR0, AR2, AR4, AR5 and AR6 (AR1 and AR3: 24 when narrower than
 * 32): 18 on the G100 and G200, 22 on the G400, 32 (default) ideal. */
extern int refrast_ar_bits;
/* Called with every 32-bit register write before the model sees it (the
 * offset as written, start alias included); NULL when unused. */
extern void (*refrast_write_hook)(uint32_t off, uint32_t v);
void refrast_init(void);
uint16_t refrast_px16(int x, int y, uint32_t pitch, uint32_t origin_px);

#endif
