/* lfb.c - linear framebuffer access (PRD §7.8).
 *
 * Read locks and 565 upper-left write locks get a pointer straight into
 * VRAM. Everything else gets a shadow buffer at the Voodoo's stride,
 * pre-filled with a per-lock sentinel so that unlock writes back only the
 * pixels the game touched, converted to 565. */
#include "glide/mg.h"
#include "mga/sys.h"
#include <string.h>

#define SHADOW_STRIDE16 2048u
#define SHADOW_STRIDE32 4096u

static struct {
    int       active;
    GrBuffer_t buffer;
    int       shadow;
    GrLfbWriteMode_t mode;
    GrOriginLocation_t origin;
    int       pipe;
    uint32_t  seed;
} lk;

static uint8_t *shadow;
static uint32_t shadow_size;
static GrColorFormat_t lfb_color_format = GR_COLORFORMAT_ARGB;
static uint16_t lfb_const_depth;
static uint8_t  lfb_const_alpha = 0xFF;

static uint32_t sentinel(uint32_t x, uint32_t y)
{
    uint32_t h = (x * 0x9E3779B1u) ^ (y * 0x85EBCA77u) ^ lk.seed;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

static int is32(GrLfbWriteMode_t m)
{
    return m == GR_LFBWRITEMODE_888 || m == GR_LFBWRITEMODE_8888 || m == GR_LFBWRITEMODE_565_DEPTH ||
           m == GR_LFBWRITEMODE_555_DEPTH || m == GR_LFBWRITEMODE_1555_DEPTH;
}

static uint16_t argb_to_565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static uint32_t order_argb(uint32_t c)
{
    GrColorFormat_t saved = mg.color_format;
    uint32_t r;
    mg.color_format = lfb_color_format;
    r = mg_color_to_argb(c);
    mg.color_format = saved;
    return r;
}

/* A 32-bit depth-buffer value as the Voodoo would hold it: the Z value's
 * top half, or in W mode the W code of the stored 2^31 * (1/w). */
static uint16_t aux_value32(uint32_t v)
{
    const mg_state *s = &mg.st;
    if (s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS)
        return (uint16_t)mg_wcode_from_oow((double)v / 2147483648.0);
    return (uint16_t)(v >> 16);
}

static int need_shadow(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t mode, GrOriginLocation_t origin, FxBool pipe)
{
    if ((type & 1) == GR_LFB_READ_ONLY) {
        if (buffer == GR_BUFFER_AUXBUFFER && mg.zbits != 16)
            return 1;
        return origin == GR_ORIGIN_LOWER_LEFT;
    }
    if (pipe)
        return 1;
    if (buffer == GR_BUFFER_AUXBUFFER)
        return mode != GR_LFBWRITEMODE_ZA16 || mg.zbits != 16 ||
               mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER;
    return !(mode == GR_LFBWRITEMODE_565 || mode == GR_LFBWRITEMODE_ANY) || origin == GR_ORIGIN_LOWER_LEFT;
}

GR_ENTRY(FxBool, grLfbLock, (GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
                             GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t *info))
{
    uint32_t off;
    if (!mg.open || !info)
        return FXFALSE;
    if (lk.active)
        grLfbUnlock(GR_LFB_WRITE_ONLY, lk.buffer);
    engine_sync(200000);
    if (origin == GR_ORIGIN_ANY)
        origin = mg.st.origin;
    if (writeMode == GR_LFBWRITEMODE_ANY)
        writeMode = buffer == GR_BUFFER_AUXBUFFER ? GR_LFBWRITEMODE_ZA16 : GR_LFBWRITEMODE_565;
    off = mg_buffer_offset(buffer);
    lk.active = 1;
    lk.buffer = buffer;
    lk.mode = writeMode;
    lk.origin = origin;
    lk.shadow = need_shadow(type, buffer, writeMode, origin, pixelPipeline);
    lk.pipe = pixelPipeline && (type & 1) == GR_LFB_WRITE_ONLY && buffer != GR_BUFFER_AUXBUFFER;
    info->size = sizeof(GrLfbInfo_t);
    info->writeMode = writeMode;
    info->origin = origin;
    if (!lk.shadow) {
        info->lfbPtr = (void *)(mga_fb + off);
        info->strideInBytes = (FxU32)mg.pitch_px * 2u;
        return FXTRUE;
    }
    {
        uint32_t stride = is32(writeMode) ? SHADOW_STRIDE32 : SHADOW_STRIDE16;
        uint32_t need = stride * (uint32_t)mg.height, x, y;
        if (need > shadow_size) {
            sys_free(shadow);
            shadow = (uint8_t *)sys_alloc(need);
            shadow_size = shadow ? need : 0;
            if (!shadow) {
                lk.active = 0;
                return FXFALSE;
            }
        }
        lk.seed = lk.seed * 1664525u + 1013904223u + mg.frame;
        if ((type & 1) == GR_LFB_READ_ONLY) {
            /* Read locks through a shadow: convert the buffer out. */
            for (y = 0; y < (uint32_t)mg.height; y++) {
                uint32_t sy = origin == GR_ORIGIN_LOWER_LEFT ? (uint32_t)mg.height - 1 - y : y;
                const uint16_t *src16 = (const uint16_t *)(mga_fb + off) + sy * (uint32_t)mg.pitch_px;
                const uint32_t *src32 = (const uint32_t *)(mga_fb + off) + sy * (uint32_t)mg.pitch_px;
                uint16_t *d = (uint16_t *)(shadow + y * SHADOW_STRIDE16);
                for (x = 0; x < (uint32_t)mg.width; x++) {
                    if (buffer == GR_BUFFER_AUXBUFFER && mg.zbits == 32)
                        d[x] = aux_value32(src32[x]);
                    else
                        d[x] = src16[x];
                }
            }
            info->lfbPtr = shadow;
            info->strideInBytes = SHADOW_STRIDE16;
            return FXTRUE;
        }
        for (y = 0; y < (uint32_t)mg.height; y++) {
            if (stride == SHADOW_STRIDE32) {
                uint32_t *d = (uint32_t *)(shadow + y * stride);
                for (x = 0; x < (uint32_t)mg.width; x++)
                    d[x] = sentinel(x, y) | (writeMode == GR_LFBWRITEMODE_888 ? 0xA5000000u : 0);
            } else {
                uint16_t *d = (uint16_t *)(shadow + y * stride);
                for (x = 0; x < (uint32_t)mg.width; x++)
                    d[x] = (uint16_t)(sentinel(x, y) | (writeMode == GR_LFBWRITEMODE_555 ? 0x8000u : 0));
            }
        }
        info->lfbPtr = shadow;
        info->strideInBytes = stride;
    }
    return FXTRUE;
}

/* ---- Pixel pipeline for LFB writes -------------------------------------
 * The retail runtime sends pipeline writes through chroma key, alpha test,
 * depth test and blending, but not through the colour combine (t18). The
 * Matrox has no such path, so it is done here on the CPU, exactly. */

static int cmp_pass(GrCmpFnc_t f, uint32_t a, uint32_t b)
{
    switch (f) {
    case GR_CMP_NEVER: return 0;
    case GR_CMP_LESS: return a < b;
    case GR_CMP_EQUAL: return a == b;
    case GR_CMP_LEQUAL: return a <= b;
    case GR_CMP_GREATER: return a > b;
    case GR_CMP_NOTEQUAL: return a != b;
    case GR_CMP_GEQUAL: return a >= b;
    default: return 1;
    }
}

static GrCmpFnc_t reversed(GrCmpFnc_t f)
{
    switch (f) {
    case GR_CMP_LESS: return GR_CMP_GREATER;
    case GR_CMP_LEQUAL: return GR_CMP_GEQUAL;
    case GR_CMP_GREATER: return GR_CMP_LESS;
    case GR_CMP_GEQUAL: return GR_CMP_LEQUAL;
    default: return f;
    }
}

/* Blend factor for one channel (0..255). 'other' is the opposite colour
 * (destination for a source factor and vice versa). */
static int blend_factor(GrAlphaBlendFnc_t f, int is_src, int own, int other, int sa, int da)
{
    MGA_UNUSED(own);
    switch (f) {
    case GR_BLEND_ZERO: return 0;
    case GR_BLEND_SRC_ALPHA: return sa;
    case GR_BLEND_SRC_COLOR: return other;          /* DST_COLOR as a source factor, SRC_COLOR as a dest one */
    case GR_BLEND_DST_ALPHA: return da;
    case GR_BLEND_ONE: return 255;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return 255 - sa;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return 255 - other;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return 255 - da;
    case GR_BLEND_ALPHA_SATURATE:
        return is_src ? (sa < 255 - da ? sa : 255 - da) : other;  /* dest: PREFOG_COLOR ~ source colour */
    default: return 255;
    }
}

static uint32_t expand565(uint16_t c)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

/* One pipeline pixel: src is ARGB8888; depth is a Glide depth value. */
static void pipe_pixel(uint32_t src, uint16_t depth, uint16_t *dst, uint16_t *z16, uint32_t *z32)
{
    const mg_state *s = &mg.st;
    int sa = (int)(src >> 24), k;
    uint32_t out = 0;
    if (s->chroma_mode == GR_CHROMAKEY_ENABLE &&
        (src & 0xFFFFFF) == (mg_color_to_argb(s->chroma_value) & 0xFFFFFF))
        return;
    if (s->alpha_test_func != GR_CMP_ALWAYS && !cmp_pass(s->alpha_test_func, (uint32_t)sa, s->alpha_test_ref))
        return;
    if (s->depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux) {
        int wmode = s->depth_mode == GR_DEPTHBUFFER_WBUFFER || s->depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
        uint32_t dv = mg_depth_clear_value(depth), cur = mg.zbits == 32 ? *z32 : *z16;
        if (!cmp_pass(wmode ? reversed(s->depth_func) : s->depth_func, dv, cur))
            return;
        if (s->depth_mask) {
            if (mg.zbits == 32) *z32 = dv;
            else *z16 = (uint16_t)dv;
        }
    }
    if (!s->color_mask_rgb)
        return;
    if (s->blend_src == GR_BLEND_ONE && s->blend_dst == GR_BLEND_ZERO) {
        out = src;
    } else {
        uint32_t d = expand565(*dst);
        for (k = 0; k < 24; k += 8) {
            int sc = (int)((src >> k) & 0xFF), dc = (int)((d >> k) & 0xFF), v;
            int fs = blend_factor(s->blend_src, 1, sc, dc, sa, 255);
            int fd = blend_factor(s->blend_dst, 0, dc, sc, sa, 255);
            v = (sc * fs + dc * fd + 127) / 255;
            out |= (uint32_t)(v > 255 ? 255 : v) << k;
        }
    }
    *dst = argb_to_565(out);
}

/* Decode one written LFB value to ARGB8888 (+ depth). */
static uint32_t decode_lfb(uint32_t v, uint16_t *depth)
{
    uint32_t r, g, b;
    *depth = lfb_const_depth;
    switch (lk.mode) {
    case GR_LFBWRITEMODE_565:
        return ((uint32_t)lfb_const_alpha << 24) | expand565((uint16_t)v);
    case GR_LFBWRITEMODE_555:
    case GR_LFBWRITEMODE_1555:
    case GR_LFBWRITEMODE_555_DEPTH:
    case GR_LFBWRITEMODE_1555_DEPTH:
        r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
        if (lk.mode == GR_LFBWRITEMODE_555_DEPTH || lk.mode == GR_LFBWRITEMODE_1555_DEPTH)
            *depth = (uint16_t)(v >> 16);
        return ((lk.mode == GR_LFBWRITEMODE_1555 || lk.mode == GR_LFBWRITEMODE_1555_DEPTH)
                    ? ((v & 0x8000) ? 0xFF000000u : 0) : ((uint32_t)lfb_const_alpha << 24)) |
               ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    case GR_LFBWRITEMODE_565_DEPTH:
        *depth = (uint16_t)(v >> 16);
        return ((uint32_t)lfb_const_alpha << 24) | expand565((uint16_t)v);
    case GR_LFBWRITEMODE_888:
        return ((uint32_t)lfb_const_alpha << 24) | (order_argb(v) & 0xFFFFFF);
    default:                                            /* 8888 */
        return order_argb(v);
    }
}

static void write_back(void)
{
    uint32_t off = mg_buffer_offset(lk.buffer), x, y, pitch = (uint32_t)mg.pitch_px;
    int depth_buf = lk.buffer == GR_BUFFER_AUXBUFFER;
    for (y = 0; y < (uint32_t)mg.height; y++) {
        uint32_t dy = lk.origin == GR_ORIGIN_LOWER_LEFT ? (uint32_t)mg.height - 1 - y : y;
        uint16_t *dst16 = (uint16_t *)(mga_fb + off) + dy * pitch;
        uint32_t *dst32 = (uint32_t *)(mga_fb + off) + dy * pitch;
        uint16_t *z16 = (uint16_t *)(mga_fb + mg.aux_off) + dy * pitch;
        uint32_t *z32 = (uint32_t *)(mga_fb + mg.aux_off) + dy * pitch;
        if (lk.pipe) {
            const uint32_t *s32 = (const uint32_t *)(shadow + y * SHADOW_STRIDE32);
            const uint16_t *s16 = (const uint16_t *)(shadow + y * SHADOW_STRIDE16);
            for (x = 0; x < (uint32_t)mg.width; x++) {
                uint32_t v, sv;
                uint16_t d;
                if (is32(lk.mode)) {
                    v = s32[x]; sv = sentinel(x, y) | (lk.mode == GR_LFBWRITEMODE_888 ? 0xA5000000u : 0);
                } else {
                    v = s16[x]; sv = (uint16_t)(sentinel(x, y) | (lk.mode == GR_LFBWRITEMODE_555 ? 0x8000u : 0));
                }
                if (v == sv)
                    continue;
                pipe_pixel(decode_lfb(v, &d), d, &dst16[x], &z16[x], &z32[x]);
            }
            continue;
        }
        if (is32(lk.mode)) {
            const uint32_t *s = (const uint32_t *)(shadow + y * SHADOW_STRIDE32);
            for (x = 0; x < (uint32_t)mg.width; x++) {
                uint32_t v = s[x], sv = sentinel(x, y) | (lk.mode == GR_LFBWRITEMODE_888 ? 0xA5000000u : 0);
                uint16_t c;
                if (v == sv)
                    continue;
                switch (lk.mode) {
                case GR_LFBWRITEMODE_888:
                case GR_LFBWRITEMODE_8888:
                    dst16[x] = argb_to_565(order_argb(v));
                    break;
                default: {                                  /* colour + depth */
                    uint16_t cv = (uint16_t)(v & 0xFFFF), d = (uint16_t)(v >> 16);
                    if (lk.mode == GR_LFBWRITEMODE_565_DEPTH) c = cv;
                    else c = (uint16_t)(((cv & 0x7FE0) << 1) | ((cv >> 4) & 0x20) | (cv & 0x1F));
                    dst16[x] = c;
                    if (mg.has_aux) {
                        if (mg.zbits == 32) z32[x] = mg_depth_clear_value(d);
                        else z16[x] = (uint16_t)mg_depth_clear_value(d);
                    }
                    break; }
                }
            }
        } else {
            const uint16_t *s = (const uint16_t *)(shadow + y * SHADOW_STRIDE16);
            for (x = 0; x < (uint32_t)mg.width; x++) {
                uint16_t v = s[x], sv = (uint16_t)(sentinel(x, y) | (lk.mode == GR_LFBWRITEMODE_555 ? 0x8000u : 0));
                if (v == sv)
                    continue;
                if (depth_buf) {
                    if (mg.zbits == 32) dst32[x] = mg_depth_clear_value(v);
                    else dst16[x] = (uint16_t)mg_depth_clear_value(v);
                } else if (lk.mode == GR_LFBWRITEMODE_565) {
                    dst16[x] = v;
                } else {                                   /* 555 / 1555 */
                    dst16[x] = (uint16_t)(((v & 0x7FE0) << 1) | ((v >> 4) & 0x20) | (v & 0x1F));
                }
            }
        }
    }
}

GR_ENTRY(FxBool, grLfbUnlock, (GrLock_t type, GrBuffer_t buffer))
{
    MGA_UNUSED(buffer);
    if (!lk.active)
        return FXFALSE;
    if (lk.shadow && (type & 1) == GR_LFB_WRITE_ONLY) {
        engine_sync(200000);
        write_back();
    }
    lk.active = 0;
    return FXTRUE;
}

GR_ENTRY(FxBool, grLfbReadRegion, (GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width,
                                   FxU32 src_height, FxU32 dst_stride, void *dst_data))
{
    uint32_t off, y, pitch;
    if (!mg.open || src_x + src_width > (FxU32)mg.width || src_y + src_height > (FxU32)mg.height)
        return FXFALSE;
    engine_sync(200000);
    off = mg_buffer_offset(src_buffer);
    pitch = (uint32_t)mg.pitch_px;
    for (y = 0; y < src_height; y++) {
        uint32_t sy = src_y + y;
        uint16_t *d = (uint16_t *)((uint8_t *)dst_data + y * dst_stride);
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
            sy = (uint32_t)mg.height - 1 - sy;
        if (src_buffer == GR_BUFFER_AUXBUFFER && mg.zbits == 32) {
            const uint32_t *s = (const uint32_t *)(mga_fb + off) + sy * pitch + src_x;
            uint32_t x;
            for (x = 0; x < src_width; x++)
                d[x] = aux_value32(s[x]);
        } else {
            memcpy(d, (const uint16_t *)(mga_fb + off) + sy * pitch + src_x, src_width * 2);
        }
    }
    return FXTRUE;
}

GR_ENTRY(FxBool, grLfbWriteRegion, (GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y, GrLfbSrcFmt_t src_format,
                                    FxU32 src_width, FxU32 src_height, FxI32 src_stride, void *src_data))
{
    uint32_t off, x, y, pitch;
    if (!mg.open || dst_x + src_width > (FxU32)mg.width || dst_y + src_height > (FxU32)mg.height)
        return FXFALSE;
    engine_sync(200000);
    off = mg_buffer_offset(dst_buffer);
    pitch = (uint32_t)mg.pitch_px;
    for (y = 0; y < src_height; y++) {
        uint32_t dy = dst_y + y;
        const uint8_t *s = (const uint8_t *)src_data + (int32_t)y * src_stride;
        uint16_t *d;
        if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
            dy = (uint32_t)mg.height - 1 - dy;
        d = (uint16_t *)(mga_fb + off) + dy * pitch + dst_x;
        for (x = 0; x < src_width; x++) {
            switch (src_format) {
            case GR_LFB_SRC_FMT_565: d[x] = ((const uint16_t *)s)[x]; break;
            case GR_LFB_SRC_FMT_555:
            case GR_LFB_SRC_FMT_1555: {
                uint16_t v = ((const uint16_t *)s)[x];
                d[x] = (uint16_t)(((v & 0x7FE0) << 1) | ((v >> 4) & 0x20) | (v & 0x1F));
                break; }
            case GR_LFB_SRC_FMT_888:
            case GR_LFB_SRC_FMT_8888:
                d[x] = argb_to_565(order_argb(((const uint32_t *)s)[x]));
                break;
            case GR_LFB_SRC_FMT_ZA16:
                d[x] = ((const uint16_t *)s)[x];
                break;
            default:
                mg_log(MG_LOG_WARN, "lfb: write format %d unsupported", (int)src_format);
                return FXFALSE;
            }
        }
    }
    return FXTRUE;
}

GR_ENTRY(void, grLfbWriteColorFormat, (GrColorFormat_t colorFormat)) { lfb_color_format = colorFormat; }
GR_ENTRY(void, grLfbWriteColorSwizzle, (FxBool swizzleBytes, FxBool swapWords))
{
    if (swizzleBytes || swapWords)
        mg_log(MG_LOG_WARN, "lfb: colour swizzle not supported");
}
GR_ENTRY(void, grLfbConstantAlpha, (GrAlpha_t alpha)) { lfb_const_alpha = alpha; }
GR_ENTRY(void, grLfbConstantDepth, (FxU16 depth)) { lfb_const_depth = depth; }

/* Glide 2.0-era region helpers imported by GTA. */
GR_ENTRY(FxBool, guFbReadRegion, (int src_x, int src_y, int w, int h, void *dst, int strideInBytes))
{
    return grLfbReadRegion(mg.render_buffer, (FxU32)src_x, (FxU32)src_y, (FxU32)w, (FxU32)h,
                           (FxU32)strideInBytes, dst);
}

GR_ENTRY(FxBool, guFbWriteRegion, (int dst_x, int dst_y, int w, int h, const void *src, int strideInBytes))
{
    return grLfbWriteRegion(mg.render_buffer, (FxU32)dst_x, (FxU32)dst_y, GR_LFB_SRC_FMT_565,
                            (FxU32)w, (FxU32)h, strideInBytes, (void *)src);
}
