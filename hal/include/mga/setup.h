/* setup.h - chip-independent triangle -> trapezoid setup (PRD §7.2, D2).
 *
 * Input vertices are in screen space with x, y in 1/16-pixel units. Each
 * triangle becomes at most two integer trapezoids whose edge DDAs are
 * exact, so neighbouring triangles are watertight under the top-left fill
 * rule. Attributes are planes evaluated at pixel centres. */
#ifndef MGA_SETUP_H
#define MGA_SETUP_H
#include "mga/types.h"

typedef struct {
    int32_t X16, Y16;      /* position, 1/16 pixel */
    double  z;             /* 16-bit Z units (0..65535) or 32-bit units (0..2^32-1) */
    float   r, g, b, a;    /* 0..255 */
    float   fog;           /* 0..255 fog factor (255 = no fog) */
    float   sr, sg, sb;    /* 0..255 specular colour added after texturing (G200+) */
    float   s, t, q;       /* texture: s/w, t/w, 1/w, already scaled (see texture setup) */
} mga_svtx;

enum {
    MGA_S_Z      = 1u << 0,    /* interpolate depth */
    MGA_S_Z32    = 1u << 1,    /* 32-bit depth (DR*_Z32 registers) */
    MGA_S_COLOR  = 1u << 2,    /* interpolate r, g, b */
    MGA_S_ALPHA  = 1u << 3,    /* interpolate alpha (TEXTURE_TRAP only on G100) */
    MGA_S_FOG    = 1u << 4,    /* interpolate fog */
    MGA_S_TEX    = 1u << 5,    /* interpolate s, t, q */
    MGA_S_AFFINE = 1u << 6,    /* q constant: texture is affine (NPCEN) */
    MGA_S_VOODOO_EDGES = 1u << 7,  /* Voodoo column rule instead of exact centres */
    MGA_S_SPEC   = 1u << 8     /* interpolate the specular colour (G200+, TEXCTL2.specen) */
};

typedef struct {
    uint32_t dwgctl;           /* DWGCTL for the trapezoids (opcode, atype, zmode, trans) */
    uint32_t flags;            /* MGA_S_* */
    int      clip_y0, clip_y1; /* rows [y0, y1) that may be drawn */
    int      tex_tw, tex_th;   /* texture log2 width/height (hardware) */
} mga_tri_ctx;

/* Statistics for benchmarks and tests. */
typedef struct { uint32_t tris, traps, culled_empty; } mga_setup_stats;
extern mga_setup_stats setup_stats;

void setup_triangle(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx);

/* Exposed for unit tests: one edge at its first row. */
typedef struct { int32_t x; int32_t ar_step, ar_err, ar_dec; int neg; } mga_edge;
void setup_edge(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e);
void setup_edge_voodoo(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e);

#endif
