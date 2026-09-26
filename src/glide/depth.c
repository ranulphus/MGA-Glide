/* depth.c - W-buffer emulation (PRD §7.3).
 *
 * The Voodoo stores a 16-bit floating-point code of 1/w: a 4-bit count of
 * leading zeros and a 12-bit inverted mantissa, plus one. MGA-Glide keeps
 * a linear 1/w instead, scaled into a 32-bit depth buffer with the compare
 * sense reversed (nearer is larger), which is more precise everywhere. */
#include "glide/mg.h"

/* Depth-buffer value for a vertex's 1/w. */
double mg_wdepth_from_oow(float oow, int zbits)
{
    double o = oow;
    if (o < 0) o = 0;
    if (o > 1.999) o = 1.999;
    return zbits == 32 ? o * 2147483648.0 : o * 32768.0;
}

/* 1/w represented by a Voodoo W-buffer code (inverse of the hardware's
 * encoding; code 0 is the nearest possible value). */
double mg_oow_from_wcode(unsigned code)
{
    unsigned e, m;
    if (code == 0)
        return 1.999;
    e = ((code - 1) >> 12) & 0xF;
    m = (code - 1) & 0xFFF;
    /* oow * 2^32 = 2^(31-e) + (~m & 0xFFF) << (19-e) */
    return (2147483648.0 / (double)(1u << e) + (double)((~m) & 0xFFF) * (double)(1u << (19 - e))) / 4294967296.0;
}

unsigned mg_wcode_from_oow(double oow)
{
    uint64_t w;
    int e;
    if (oow >= 1.0)
        return 0;
    w = (uint64_t)(oow * 4294967296.0);
    if (!(w & 0xFFFF0000u))
        return 0xF001;
    for (e = 0; e < 16 && !((w >> 16) & (0x8000u >> e)); e++)
        ;
    {
        unsigned mant = (unsigned)((~(uint32_t)w) >> (19 - e)) & 0xFFF;
        unsigned d = ((unsigned)e << 12) + mant + 1;
        return d > 0xFFFF ? 0xFFFF : d;
    }
}

uint32_t mg_wdepth_from_code(FxU16 code, int zbits)
{
    double z = mg_wdepth_from_oow((float)mg_oow_from_wcode(code), zbits);
    return (uint32_t)(z + 0.5);
}
