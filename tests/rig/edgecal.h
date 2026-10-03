/* edgecal.h - triangles for checking edge terms against a chip's AR fields
 * (docs/loop-c-results.md): drawn flat by tests/rig/mgarig.c (edgecal)
 * into a 1024x1024 16-bit target, once with the limit off (the values
 * before the fix) and once as the HAL draws them now, and compared by
 * tests/rig/edgecmp.c with refrast at the chip's width and at 32 bits. */
#ifndef MGA_EDGECAL_H
#define MGA_EDGECAL_H
#include "mga/setup.h"

typedef struct {
    const char *name;
    uint32_t    flags;                 /* 0: exact edges; MGA_S_VOODOO_EDGES */
    int32_t     X16[3], Y16[3];        /* 1/16 pixel */
} edgecal_tri;

#define EC_TRI(name, flags, x0, y0, x1, y1, x2, y2) { name, flags, { (x0), (x1), (x2) }, { (y0), (y1), (y2) } }

static const edgecal_tri edgecal_tris[] = {
    /* Tall and wide right triangles, exact edges (bigtri's shapes). */
    EC_TRI("exact-64x600",    0, 0, 0, 64 * 16, 0, 0, 600 * 16),
    EC_TRI("exact-64x768",    0, 0, 0, 64 * 16, 0, 0, 768 * 16),
    EC_TRI("exact-64x1020",   0, 0, 0, 64 * 16, 0, 0, 1020 * 16),
    EC_TRI("exact-1020x600",  0, 0, 0, 1020 * 16, 0, 0, 600 * 16),
    EC_TRI("exact-500x500",   0, 0, 0, 500 * 16, 0, 0, 500 * 16),
    EC_TRI("exact-520x300",   0, 0, 0, 520 * 16, 0, 0, 300 * 16),
    EC_TRI("exact-wide",      0, 5 * 16, 5 * 16, 630 * 16, 40 * 16, 300 * 16, 470 * 16),
    /* Sub-pixel vertices, and one far off the target (clipped rows). */
    EC_TRI("exact-subpixel",  0, 37, 21, 15003, 1907, 2211, 16290),
    EC_TRI("exact-offscreen", 0, -1500 * 16, -900 * 16, 1010 * 16, 40 * 16, 200 * 16, 2400 * 16),
    /* Voodoo edges at slopes 1.5, 2.5, about 14, 40 and 64 pixels a row. */
    EC_TRI("voodoo-1.5",      MGA_S_VOODOO_EDGES, 10 * 16, 10 * 16, 40 * 16, 30 * 16, 20 * 16, 60 * 16),
    EC_TRI("voodoo-2.5",      MGA_S_VOODOO_EDGES, 10 * 16, 10 * 16, 60 * 16, 30 * 16, 20 * 16, 60 * 16),
    EC_TRI("voodoo-14",       MGA_S_VOODOO_EDGES, 10 * 16, 10 * 16, 300 * 16, 30 * 16, 20 * 16, 60 * 16),
    EC_TRI("voodoo-40",       MGA_S_VOODOO_EDGES, 100 * 16, 200 * 16, 900 * 16, 220 * 16, 250 * 16, 260 * 16),
    EC_TRI("voodoo-64",       MGA_S_VOODOO_EDGES, 0, 100 * 16, 639 * 16, 110 * 16, 0, 120 * 16),
    EC_TRI("voodoo-wide",     MGA_S_VOODOO_EDGES, 5 * 16, 5 * 16, 630 * 16, 40 * 16, 300 * 16, 470 * 16),
    EC_TRI("voodoo-tall",     MGA_S_VOODOO_EDGES, 0, 0, 64 * 16, 0, 0, 1020 * 16),
    EC_TRI("voodoo-subpixel", MGA_S_VOODOO_EDGES, 37, 21, 15003, 1907, 2211, 16290),
};
#define EDGECAL_N ((int)(sizeof edgecal_tris / sizeof edgecal_tris[0]))
#define EDGECAL_W 1024
#define EDGECAL_H 1024

#endif
