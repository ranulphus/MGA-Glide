/* linux.c - HAL port for Loop C (PRD §4.4): a G200-family card driven from
 * Linux through its sysfs resource files. Only the device named by
 * RIG_BDF (default 0000:0a:00.0) is touched: its BARs are mapped from
 * /sys/bus/pci/devices/<bdf>/resourceN; there is no port I/O, no BIOS and
 * no interrupt handling. Needs root for the mappings. */
#define _GNU_SOURCE
#include "mga/sys.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static const char *bdf(void)
{
    const char *b = getenv("RIG_BDF");
    return b && *b ? b : "0000:0a:00.0";
}

uint8_t  sys_inb(uint16_t p) { MGA_UNUSED(p); return 0xFF; }
uint16_t sys_inw(uint16_t p) { MGA_UNUSED(p); return 0xFFFF; }
uint32_t sys_inl(uint16_t p) { MGA_UNUSED(p); return 0xFFFFFFFFu; }
void sys_outb(uint16_t p, uint8_t v) { MGA_UNUSED(p); MGA_UNUSED(v); }
void sys_outw(uint16_t p, uint16_t v) { MGA_UNUSED(p); MGA_UNUSED(v); }
void sys_outl(uint16_t p, uint32_t v) { MGA_UNUSED(p); MGA_UNUSED(v); }

/* Map [phys, phys+size) through the resource file of the BAR containing it. */
volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    char path[160];
    FILE *f;
    unsigned long long start, end, flags;
    int bar = 0, fd;
    void *p;
    snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/resource", bdf());
    f = fopen(path, "r");
    if (!f)
        return NULL;
    while (fscanf(f, "%llx %llx %llx", &start, &end, &flags) == 3) {
        if (start && phys >= start && (unsigned long long)phys + size - 1 <= end)
            break;
        bar++;
    }
    fclose(f);
    if (bar > 5)
        return NULL;
    snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/resource%d", bdf(), bar);
    fd = open(path, getenv("RIG_READONLY") ? O_RDONLY : (O_RDWR | O_SYNC));
    if (fd < 0)
        return NULL;
    p = mmap(NULL, size, getenv("RIG_READONLY") ? PROT_READ : (PROT_READ | PROT_WRITE), MAP_SHARED, fd,
             (off_t)(phys - start));
    close(fd);
    return p == MAP_FAILED ? NULL : (volatile void *)p;
}

void sys_unmap_phys(volatile void *p, uint32_t size)
{
    if (p)
        munmap((void *)p, size);
}

int sys_rm_int(uint8_t intno, sys_rmregs *r) { MGA_UNUSED(intno); MGA_UNUSED(r); return -1; }
void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment) { MGA_UNUSED(bytes); MGA_UNUSED(rm_segment); return NULL; }
void sys_dos_free(void *p) { MGA_UNUSED(p); }
void *sys_real_ptr(uint16_t seg, uint16_t off) { MGA_UNUSED(seg); MGA_UNUSED(off); return NULL; }

uint32_t sys_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}

void sys_delay_us(uint32_t us) { usleep(us); }
void *sys_alloc(uint32_t bytes) { return calloc(1, bytes); }
void sys_free(void *p) { free(p); }

int  sys_hook_faults(sys_fault_fn fn) { (void)fn; return -1; }
void sys_unhook_faults(void) { }
int  sys_hook_exit(sys_exit_fn fn) { (void)fn; return -1; }
void sys_unhook_exit(void) { }
