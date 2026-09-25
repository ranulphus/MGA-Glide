/* host.h - hooks for the Linux host port (hal/port/host.c). */
#ifndef MGA_HOST_H
#define MGA_HOST_H
#include "mga/types.h"

typedef struct {
    void *ctx;
    uint32_t (*in)(void *ctx, uint16_t port, int size);
    void (*out)(void *ctx, uint16_t port, uint32_t value, int size);
} host_io_hooks;

void host_set_io_hooks(const host_io_hooks *h);
/* Make sys_map_phys(phys..phys+size) return pointers into mem. */
void host_register_phys(uint32_t phys, uint32_t size, void *mem);
void host_clear_phys(void);

#endif
