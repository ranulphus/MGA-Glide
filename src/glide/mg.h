/* mg.h - MGA-Glide runtime context: the Glide state mirror, the display
 * surfaces and the translation state shared by the src/glide modules. */
#ifndef MG_H
#define MG_H
#include "glide/entry.h"
#include "mga/hal.h"
#include "rt/rt.h"
#include "dll/config.h"

#ifndef MGA_GLIDE_VERSION
#define MGA_GLIDE_VERSION "dev"
#endif

#define MG_MAX_BUFFERS 3

typedef struct {
    /* Colour / alpha combine (grColorCombine / grAlphaCombine). */
    GrCombineFunction_t cc_func;  GrCombineFactor_t cc_factor;
    GrCombineLocal_t    cc_local; GrCombineOther_t  cc_other; FxBool cc_invert;
    GrCombineFunction_t ac_func;  GrCombineFactor_t ac_factor;
    GrCombineLocal_t    ac_local; GrCombineOther_t  ac_other; FxBool ac_invert;
    GrColor_t           constant_color;
    /* Texture combine (TMU0). */
    GrCombineFunction_t tc_rgb_func, tc_alpha_func;
    GrCombineFactor_t   tc_rgb_factor, tc_alpha_factor;
    FxBool              tc_rgb_invert, tc_alpha_invert;
    /* Blending and alpha test. */
    GrAlphaBlendFnc_t   blend_src, blend_dst, blend_asrc, blend_adst;
    GrCmpFnc_t          alpha_test_func;
    GrAlpha_t           alpha_test_ref;
    /* Depth. */
    GrDepthBufferMode_t depth_mode;
    GrCmpFnc_t          depth_func;
    FxBool              depth_mask;
    FxI32               depth_bias;
    /* Rasterisation. */
    GrCullMode_t        cull;
    GrDitherMode_t      dither;
    FxBool              color_mask_rgb, color_mask_a;
    int                 clip_x0, clip_y0, clip_x1, clip_y1;
    GrOriginLocation_t  origin;
    /* Fog. */
    GrFogMode_t         fog_mode;
    GrColor_t           fog_color;
    GrFog_t             fog_table[GR_FOG_TABLE_SIZE];
    /* Chroma key. */
    GrChromakeyMode_t   chroma_mode;
    GrColor_t           chroma_value;
    /* Hints. */
    FxU32               stw_hint;
} mg_state;

typedef struct {
    int         initialised;
    int         open;
    int         sst;                 /* selected board */
    mga_vbe_mode mode;
    int         width, height, pitch_px;
    int         nbuffers, has_aux;
    uint32_t    buf_off[MG_MAX_BUFFERS];
    uint32_t    aux_off;
    int         zbits;               /* 16 or 32 */
    int         front, back;         /* indices into buf_off */
    GrBuffer_t  render_buffer;
    uint32_t    heap_off, heap_end;  /* texture heap in VRAM */
    GrColorFormat_t color_format;
    uint32_t    frame;
    mg_state    st;
    unsigned    dirty;
    GrErrorCallbackFnc_t error_cb;
    float       gamma;
    char        version[32];
} mg_ctx;

extern mg_ctx mg;

enum { MG_DIRTY_TARGET = 1u << 0, MG_DIRTY_CLIP = 1u << 1, MG_DIRTY_RASTER = 1u << 2 };

/* sst.c */
int  mg_device_open(void);
void mg_fatal(const char *msg);
/* state.c */
void mg_state_defaults(void);
void mg_validate(void);
uint32_t mg_color_to_argb(GrColor_t c);
uint32_t mg_argb_to_565(uint32_t argb);
/* buffer.c */
void mg_target_for(GrBuffer_t b);
uint32_t mg_buffer_offset(GrBuffer_t b);
/* draw.c */
void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c);

/* depth.c */
double   mg_wdepth_from_oow(float oow, int zbits);
double   mg_oow_from_wcode(unsigned code);
unsigned mg_wcode_from_oow(double oow);
uint32_t mg_wdepth_from_code(FxU16 code, int zbits);
uint32_t mg_depth_clear_value(FxU16 depth);
/* combine.c */
void mg_note_approx(void);
void mg_census(void);
/* frame.c */
typedef struct { uint32_t tris; } mg_stats_t;
extern mg_stats_t mg_stats;
void mg_frame_end(void);

/* FPU control for entry points that do float maths (see fp.h). */
#include "mga/fp.h"

void mg_gamma_apply(void);

#endif
