/* texconv.c - see texconv.h. Decoding follows the Voodoo's documented
 * expansion rules (bit replication), which 86Box's Voodoo model also uses. */
#include "tex/texconv.h"
#include "tex/texfmt.h"

static uint32_t argb(int a, int r, int g, int b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t rgb332(uint32_t c)
{
    int r = (int)(c & 0xE0), g = (int)((c << 3) & 0xE0), b = (int)((c << 6) & 0xC0);
    r |= (r >> 3) | (r >> 6);
    g |= (g >> 3) | (g >> 6);
    b |= b >> 2;
    b |= b >> 4;
    return argb(0xFF, r, g, b);
}

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static int sx9(uint32_t v) { v &= 0x1FF; return (v & 0x100) ? (int)v - 0x200 : (int)v; }

static uint32_t ncc(uint32_t c, const tex_tables *t)
{
    const uint32_t *tb = t->ncc[t->ncc_sel & 1];
    int yi = (int)(c >> 4), ii = (int)((c >> 2) & 3), qi = (int)(c & 3);
    int y = (int)((tb[yi >> 2] >> ((yi & 3) * 8)) & 0xFF);
    uint32_t I = tb[4 + ii], Q = tb[8 + qi];
    return argb(0xFF, clamp255(y + sx9(I >> 18) + sx9(Q >> 18)),
                clamp255(y + sx9(I >> 9) + sx9(Q >> 9)), clamp255(y + sx9(I) + sx9(Q)));
}

uint32_t tex_decode(GrTextureFormat_t fmt, uint32_t c, const tex_tables *t)
{
    switch (fmt) {
    case GR_TEXFMT_RGB_332: return rgb332(c & 0xFF);
    case GR_TEXFMT_YIQ_422: return ncc(c & 0xFF, t);
    case GR_TEXFMT_ALPHA_8: c &= 0xFF; return argb((int)c, (int)c, (int)c, (int)c);
    case GR_TEXFMT_INTENSITY_8: c &= 0xFF; return argb(0xFF, (int)c, (int)c, (int)c);
    case GR_TEXFMT_ALPHA_INTENSITY_44: {
        int i = (int)(c & 0x0F), a = (int)(c & 0xF0);
        i |= i << 4;
        a |= a >> 4;
        return argb(a, i, i, i); }
    case GR_TEXFMT_P_8: return 0xFF000000u | (t->palette[c & 0xFF] & 0xFFFFFF);
    case GR_TEXFMT_ARGB_8332: return (rgb332(c & 0xFF) & 0xFFFFFF) | ((c >> 8) << 24);
    case GR_TEXFMT_AYIQ_8422: return (ncc(c & 0xFF, t) & 0xFFFFFF) | (((c >> 8) & 0xFF) << 24);
    case GR_TEXFMT_RGB_565: {
        int r = (int)((c >> 8) & 0xF8), g = (int)((c >> 3) & 0xFC), b = (int)((c << 3) & 0xF8);
        return argb(0xFF, r | (r >> 5), g | (g >> 6), b | (b >> 5)); }
    case GR_TEXFMT_ARGB_1555: {
        int r = (int)((c >> 7) & 0xF8), g = (int)((c >> 2) & 0xF8), b = (int)((c << 3) & 0xF8);
        return argb((c & 0x8000) ? 0xFF : 0, r | (r >> 5), g | (g >> 5), b | (b >> 5)); }
    case GR_TEXFMT_ARGB_4444: {
        int a = (int)((c >> 8) & 0xF0), r = (int)((c >> 4) & 0xF0), g = (int)(c & 0xF0), b = (int)((c << 4) & 0xF0);
        return argb(a | (a >> 4), r | (r >> 4), g | (g >> 4), b | (b >> 4)); }
    case GR_TEXFMT_ALPHA_INTENSITY_88: {
        int i = (int)(c & 0xFF);
        return argb((int)((c >> 8) & 0xFF), i, i, i); }
    case GR_TEXFMT_AP_88: return (t->palette[c & 0xFF] & 0xFFFFFF) | (((c >> 8) & 0xFF) << 24);
    default: return 0xFFFFFFFFu;
    }
}

int tex_classify(const uint32_t *px, int n)
{
    int i, binary = 1;
    for (i = 0; i < n; i++) {
        uint32_t a = px[i] >> 24;
        if (a != 0xFF) {
            if (a != 0)
                return HW_TW12;
            binary = 2;
        }
    }
    return binary == 2 ? HW_TW15 : HW_TW16;
}

static uint16_t enc(int hwfmt, uint32_t c)
{
    switch (hwfmt) {
    case HW_TW15:
        return (uint16_t)(((c >> 31) << 15) | ((c >> 9) & 0x7C00) | ((c >> 6) & 0x03E0) | ((c >> 3) & 0x1F));
    case HW_TW12:
        return (uint16_t)(((c >> 16) & 0xF000) | ((c >> 12) & 0x0F00) | ((c >> 8) & 0x00F0) | ((c >> 4) & 0x0F));
    default:
        return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F));
    }
}

static int src_index(int x, int len, int clamp)
{
    if (x < len)
        return x;
    return clamp ? len - 1 : x % len;
}

int tex_convert_level(GrTextureFormat_t fmt, const void *src, int w, int h,
                      uint16_t *dst, int dst_pitch, int hw_w, int hw_h, int clamp_s, int clamp_t,
                      const tex_tables *t, int force_hwfmt, uint32_t *scratch)
{
    int x, y, hwfmt, native = -1;
    const uint8_t *s8 = (const uint8_t *)src;
    const uint16_t *s16 = (const uint16_t *)src;
    /* Formats the Matrox holds natively are copied bit-exactly. */
    if (fmt == GR_TEXFMT_RGB_565) native = HW_TW16;
    else if (fmt == GR_TEXFMT_ARGB_1555) native = HW_TW15;
    else if (fmt == GR_TEXFMT_ARGB_4444) native = HW_TW12;
    if (native >= 0 && (force_hwfmt < 0 || force_hwfmt == native)) {
        for (y = 0; y < hw_h; y++) {
            const uint16_t *row = s16 + src_index(y, h, clamp_t) * w;
            for (x = 0; x < hw_w; x++)
                dst[y * dst_pitch + x] = row[src_index(x, w, clamp_s)];
        }
        return native;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            scratch[y * w + x] = tex_decode(fmt, tex_bpp(fmt) == 2 ? s16[y * w + x] : s8[y * w + x], t);
    hwfmt = force_hwfmt >= 0 ? force_hwfmt : tex_classify(scratch, w * h);
    for (y = 0; y < hw_h; y++) {
        const uint32_t *row = scratch + src_index(y, h, clamp_t) * w;
        for (x = 0; x < hw_w; x++)
            dst[y * dst_pitch + x] = enc(hwfmt, row[src_index(x, w, clamp_s)]);
    }
    return hwfmt;
}
