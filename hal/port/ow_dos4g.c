/* ow_dos4g.c - HAL port for Open Watcom code running inside a DOS/4GW
 * process (the MGA-Glide runtime and its DOS test programs).
 *
 * DOS/4GW runs a flat, zero-based model: a linear address returned by
 * DPMI 0800h is directly usable as a near pointer. */
#include "mga/sys.h"
#include <conio.h>
#include <i86.h>
#include <string.h>

uint8_t  sys_inb(uint16_t p) { return (uint8_t)inp(p); }
uint16_t sys_inw(uint16_t p) { return (uint16_t)inpw(p); }
uint32_t sys_inl(uint16_t p) { return (uint32_t)inpd(p); }
void sys_outb(uint16_t p, uint8_t v) { outp(p, v); }
void sys_outw(uint16_t p, uint16_t v) { outpw(p, v); }
void sys_outl(uint16_t p, uint32_t v) { outpd(p, v); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0800;
    r.w.bx = (uint16_t)(phys >> 16);
    r.w.cx = (uint16_t)(phys & 0xFFFF);
    r.w.si = (uint16_t)(size >> 16);
    r.w.di = (uint16_t)(size & 0xFFFF);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return NULL;
    return (volatile void *)(((uint32_t)r.w.bx << 16) | r.w.cx);
}

void sys_unmap_phys(volatile void *p, uint32_t size)
{
    union REGS r;
    MGA_UNUSED(size);
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0801;
    r.w.bx = (uint16_t)((uint32_t)p >> 16);
    r.w.cx = (uint16_t)((uint32_t)p & 0xFFFF);
    int386(0x31, &r, &r);
}

int sys_rm_int(uint8_t intno, sys_rmregs *rm)
{
    union REGS r;
    struct SREGS s;
    memset(&r, 0, sizeof r);
    segread(&s);
    r.w.ax = 0x0300;
    r.h.bl = intno;
    r.h.bh = 0;
    r.w.cx = 0;
    s.es = s.ds;
    r.x.edi = (uint32_t)rm;
    int386x(0x31, &r, &r, &s);
    return r.x.cflag ? -1 : 0;
}

/* Conventional memory: DPMI 0100h returns a real-mode segment and a
 * selector; in the flat model the linear address is segment * 16. */
#define DOS_BLOCKS 8
static struct { void *p; uint16_t sel; } dos_blocks[DOS_BLOCKS];

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    union REGS r;
    int i;
    for (i = 0; i < DOS_BLOCKS && dos_blocks[i].p; i++)
        ;
    if (i == DOS_BLOCKS)
        return NULL;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0100;
    r.w.bx = (uint16_t)((bytes + 15) >> 4);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return NULL;
    *rm_segment = r.w.ax;
    dos_blocks[i].p = (void *)((uint32_t)r.w.ax << 4);
    dos_blocks[i].sel = r.w.dx;
    return dos_blocks[i].p;
}

void sys_dos_free(void *p)
{
    union REGS r;
    int i;
    for (i = 0; i < DOS_BLOCKS; i++) {
        if (dos_blocks[i].p == p) {
            memset(&r, 0, sizeof r);
            r.w.ax = 0x0101;
            r.w.dx = dos_blocks[i].sel;
            int386(0x31, &r, &r);
            dos_blocks[i].p = NULL;
            return;
        }
    }
}

/* Monotonic time from the BIOS tick counter at 0040:006C (18.2 Hz). The
 * PIT cannot be used for sub-tick resolution: the BIOS runs channel 0 in
 * mode 3, which counts through its range twice per tick, so derived time
 * would run backwards. Timeouts only need tick resolution. */
uint32_t sys_time_us(void)
{
    volatile uint32_t *ticks = (volatile uint32_t *)0x46C;
    return (uint32_t)((uint64_t)*ticks * 54925u);
}

/* Short delays: each read of port 0x80 takes about a microsecond on ISA
 * timing; longer delays wait on the tick counter. */
void sys_delay_us(uint32_t us)
{
    if (us >= 55000u) {
        uint32_t start = sys_time_us();
        while ((uint32_t)(sys_time_us() - start) < us)
            ;
        return;
    }
    while (us--)
        (void)inp(0x80);
}

/* DPMI 0501h block allocator: simple bump arena with a free list is
 * provided by src/rt for the runtime; the HAL only needs a few tables. */
void *sys_alloc(uint32_t bytes)
{
    union REGS r;
    uint32_t *blk;
    uint32_t total = bytes + 8;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0501;
    r.w.bx = (uint16_t)(total >> 16);
    r.w.cx = (uint16_t)(total & 0xFFFF);
    int386(0x31, &r, &r);
    if (r.x.cflag)
        return NULL;
    blk = (uint32_t *)(((uint32_t)r.w.bx << 16) | r.w.cx);
    blk[0] = ((uint32_t)r.w.si << 16) | r.w.di;   /* DPMI handle */
    return blk + 2;
}

void sys_free(void *p)
{
    union REGS r;
    uint32_t h;
    if (!p)
        return;
    h = ((uint32_t *)p)[-2];
    memset(&r, 0, sizeof r);
    r.w.ax = 0x0502;
    r.w.si = (uint16_t)(h >> 16);
    r.w.di = (uint16_t)(h & 0xFFFF);
    int386(0x31, &r, &r);
}

void *sys_real_ptr(uint16_t seg, uint16_t off) { return (void *)(((uint32_t)seg << 4) + off); }
