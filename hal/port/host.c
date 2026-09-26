/* host.c - HAL port for Linux host builds (unit tests, trace replay).
 *
 * There is no hardware: port I/O goes to an optional hook (the simulated
 * PCI configuration space and UART), physical mappings come from regions
 * registered by the test harness, and real-mode calls fail. */
#include "mga/sys.h"
#include "mga/host.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static host_io_hooks io_hooks;

void host_set_io_hooks(const host_io_hooks *h)
{
    if (h) io_hooks = *h; else memset(&io_hooks, 0, sizeof io_hooks);
}

static uint32_t in_n(uint16_t p, int size)
{
    return io_hooks.in ? io_hooks.in(io_hooks.ctx, p, size) : 0xFFFFFFFFu;
}

static void out_n(uint16_t p, uint32_t v, int size)
{
    if (io_hooks.out)
        io_hooks.out(io_hooks.ctx, p, v, size);
}

uint8_t  sys_inb(uint16_t p) { return (uint8_t)in_n(p, 1); }
uint16_t sys_inw(uint16_t p) { return (uint16_t)in_n(p, 2); }
uint32_t sys_inl(uint16_t p) { return in_n(p, 4); }
void sys_outb(uint16_t p, uint8_t v) { out_n(p, v, 1); }
void sys_outw(uint16_t p, uint16_t v) { out_n(p, v, 2); }
void sys_outl(uint16_t p, uint32_t v) { out_n(p, v, 4); }

#define HOST_REGIONS 8
static struct { uint32_t phys, size; void *mem; } regions[HOST_REGIONS];

void host_register_phys(uint32_t phys, uint32_t size, void *mem)
{
    int i;
    for (i = 0; i < HOST_REGIONS; i++) {
        if (!regions[i].mem || regions[i].phys == phys) {
            regions[i].phys = phys;
            regions[i].size = size;
            regions[i].mem = mem;
            return;
        }
    }
}

void host_clear_phys(void) { memset(regions, 0, sizeof regions); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    int i;
    for (i = 0; i < HOST_REGIONS; i++) {
        if (regions[i].mem && phys >= regions[i].phys &&
            (uint64_t)phys + size <= (uint64_t)regions[i].phys + regions[i].size)
            return (uint8_t *)regions[i].mem + (phys - regions[i].phys);
    }
    return NULL;
}

void sys_unmap_phys(volatile void *p, uint32_t size) { MGA_UNUSED(p); MGA_UNUSED(size); }

int sys_rm_int(uint8_t intno, sys_rmregs *r) { MGA_UNUSED(intno); MGA_UNUSED(r); return -1; }

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    *rm_segment = 0;
    return calloc(1, bytes);
}

void sys_dos_free(void *p) { free(p); }

uint32_t sys_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}

void sys_delay_us(uint32_t us) { MGA_UNUSED(us); }

void *sys_alloc(uint32_t bytes) { return calloc(1, bytes); }
void sys_free(void *p) { free(p); }

void *sys_real_ptr(uint16_t seg, uint16_t off) { MGA_UNUSED(seg); MGA_UNUSED(off); return NULL; }

int  sys_hook_faults(sys_fault_fn fn) { (void)fn; return -1; }
void sys_unhook_faults(void) { }
int  sys_hook_exit(sys_exit_fn fn) { (void)fn; return -1; }
void sys_unhook_exit(void) { }
