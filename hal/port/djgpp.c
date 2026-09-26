/* djgpp.c - HAL port for DJGPP programs under CWSDPMI (DOS-GL).
 *
 * Apertures are mapped with DPMI 0800h and reached through near pointers
 * (__djgpp_nearptr_enable), so a mapped linear address is offset by
 * __djgpp_base_address. Callers that toggle nearptr off must re-enable it
 * before calling into the HAL. */
#include "mga/sys.h"
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/nearptr.h>
#include <time.h>

uint8_t  sys_inb(uint16_t p) { return inportb(p); }
uint16_t sys_inw(uint16_t p) { return inportw(p); }
uint32_t sys_inl(uint16_t p) { return inportl(p); }
void sys_outb(uint16_t p, uint8_t v) { outportb(p, v); }
void sys_outw(uint16_t p, uint16_t v) { outportw(p, v); }
void sys_outl(uint16_t p, uint32_t v) { outportl(p, v); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    __dpmi_meminfo mi;
    if (!__djgpp_nearptr_enable())
        return NULL;
    mi.address = phys;
    mi.size = size;
    if (__dpmi_physical_address_mapping(&mi) != 0)
        return NULL;
    return (volatile void *)(mi.address - (uint32_t)__djgpp_base_address);
}

void sys_unmap_phys(volatile void *p, uint32_t size)
{
    __dpmi_meminfo mi;
    mi.address = (uint32_t)(uintptr_t)p + (uint32_t)__djgpp_base_address;
    mi.size = size;
    __dpmi_free_physical_address_mapping(&mi);
}

int sys_rm_int(uint8_t intno, sys_rmregs *r)
{
    __dpmi_regs d;
    memset(&d, 0, sizeof d);
    d.d.edi = r->edi; d.d.esi = r->esi; d.d.ebp = r->ebp;
    d.d.ebx = r->ebx; d.d.edx = r->edx; d.d.ecx = r->ecx; d.d.eax = r->eax;
    d.x.es = r->es; d.x.ds = r->ds; d.x.fs = r->fs; d.x.gs = r->gs;
    if (__dpmi_int(intno, &d) != 0)
        return -1;
    r->edi = d.d.edi; r->esi = d.d.esi; r->ebp = d.d.ebp;
    r->ebx = d.d.ebx; r->edx = d.d.edx; r->ecx = d.d.ecx; r->eax = d.d.eax;
    r->flags = d.x.flags; r->es = d.x.es; r->ds = d.x.ds;
    return 0;
}

#define DOS_BLOCKS 8
static struct { void *p; int sel; } dos_blocks[DOS_BLOCKS];

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    int i, sel, seg;
    for (i = 0; i < DOS_BLOCKS && dos_blocks[i].p; i++)
        ;
    if (i == DOS_BLOCKS || !__djgpp_nearptr_enable())
        return NULL;
    seg = __dpmi_allocate_dos_memory((int)((bytes + 15) >> 4), &sel);
    if (seg < 0)
        return NULL;
    *rm_segment = (uint16_t)seg;
    dos_blocks[i].p = (void *)((uint32_t)seg * 16u - (uint32_t)__djgpp_base_address);
    dos_blocks[i].sel = sel;
    return dos_blocks[i].p;
}

void sys_dos_free(void *p)
{
    int i;
    for (i = 0; i < DOS_BLOCKS; i++) {
        if (dos_blocks[i].p == p) {
            __dpmi_free_dos_memory(dos_blocks[i].sel);
            dos_blocks[i].p = NULL;
        }
    }
}

uint32_t sys_time_us(void)
{
    return (uint32_t)((unsigned long long)uclock() * 1000000ull / UCLOCKS_PER_SEC);
}

void sys_delay_us(uint32_t us)
{
    uint32_t start = sys_time_us();
    while ((uint32_t)(sys_time_us() - start) < us)
        ;
}

void *sys_alloc(uint32_t bytes) { return malloc(bytes); }
void sys_free(void *p) { free(p); }

void *sys_real_ptr(uint16_t seg, uint16_t off)
{
    __djgpp_nearptr_enable();
    return (void *)((((uint32_t)seg << 4) + off) - (uint32_t)__djgpp_base_address);
}
