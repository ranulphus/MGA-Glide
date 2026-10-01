/* mmio.h - access to the MGA control aperture.
 *
 * All drawing-register writes go through MGA_WR32 so the debug build can
 * record them (register write trace, PRD §9) and the host build can feed
 * them to the reference rasteriser. */
#ifndef MGA_MMIO_H
#define MGA_MMIO_H
#include "mga/types.h"
#include "mga/prof.h"

extern volatile uint8_t *mga_mmio;

/* Register write trace (hal/src/debug/regtrace.c). Recording is compiled
 * into MGA_WR32 with MGA_TRACE_MMIO or MG_DEBUG; the functions always exist. */
void     mga_trace_record(uint32_t off, uint32_t v);
void     mga_trace_record_at(uint32_t off, uint32_t v, const char *file, int line);
void     mga_trace_dump(void (*out)(const char *line), int max);   /* oldest first; max 0 = all */
uint32_t mga_trace_count(void);
void     mga_trace_clear(void);

#if defined(MGA_HOST)
void     mga_host_wr32(uint32_t off, uint32_t v);
uint32_t mga_host_rd32(uint32_t off);
void     mga_host_wr8(uint32_t off, uint8_t v);
uint8_t  mga_host_rd8(uint32_t off);
#  define MGA_WR32(off, v) mga_host_wr32((off), (v))
#  define MGA_RD32(off)    mga_host_rd32(off)
#  define MGA_WR8(off, v)  mga_host_wr8((off), (v))
#  define MGA_RD8(off)     mga_host_rd8(off)
#else
#  if defined(MG_DEBUG) || defined(MGA_TRACE_MMIO)
#    define MGA_TRACE(off, v) mga_trace_record_at((off), (v), __FILE__, __LINE__)
#  else
#    define MGA_TRACE(off, v) ((void)0)
#  endif
#  define MGA_WR32(off, v) do { MGA_TRACE((off), (v)); PROF_WR(); \
        *(volatile uint32_t *)(mga_mmio + (off)) = (uint32_t)(v); } while (0)
#  define MGA_RD32(off)    (*(volatile uint32_t *)(mga_mmio + (off)))
#  define MGA_WR8(off, v)  (*(volatile uint8_t *)(mga_mmio + (off)) = (uint8_t)(v))
#  define MGA_RD8(off)     (*(volatile uint8_t *)(mga_mmio + (off)))
#endif

#endif
