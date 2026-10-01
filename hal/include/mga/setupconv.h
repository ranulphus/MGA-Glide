/* setupconv.h - the triangle setup's float to fixed-point conversions
 * (setup/trap.c), in a header so tests/unit/test_fp.c can hold them
 * against the code they replaced.
 *
 * Each has a fast path through fp.h's FISTP helpers inside a range where
 * it gives exactly what the original gave, and the original code, verbatim,
 * outside it (huge values, NaN). The clamps are written as the original's
 * expressions, never as hand-computed constants. */
#ifndef MGA_SETUPCONV_H
#define MGA_SETUPCONV_H
#include "mga/fp.h"

/* Round to fixed point: floor(v * scale + 0.5), saturated to int32. */
MGA_INLINE int32_t fx(double v, double scale)
{
    double r, x = v * scale + 0.5;
    if (x > -2147483647.0 && x < 2147483647.0)
        return mga_ifloor(x);
    r = mga_floor(x);
    if (r > 2147483647.0) r = 2147483647.0;
    if (r < -2147483648.0) r = -2147483648.0;
    return (int32_t)r;
}

/* Start value for colour-like 9.15 channels: truncating engine, so bias by
 * half an LSB of the 8-bit result is not applied (matches Voodoo floor). */
MGA_INLINE uint32_t col_start(double v)
{
    double r, x = v * 32768.0;
    if (x > -2147483647.0 && x < 2147483647.0) {
        int32_t f = mga_ifloor(x);
        if (f < 0)
            return 0;
        return (double)f > 255.999 * 32768.0 ? (uint32_t)(255.999 * 32768.0) : (uint32_t)f;
    }
    r = mga_floor(x);
    if (r < 0) r = 0;
    if (r > 255.999 * 32768.0) r = 255.999 * 32768.0;
    return (uint32_t)r;
}

MGA_INLINE uint32_t z16_start(double z)
{
    double r, x = z * 32768.0;
    if (x > -2147483647.0 && x < 2147483647.0) {
        int32_t f = mga_ifloor(x);
        if (f < 0)
            return 0;
        return (double)f > 65535.999 * 32768.0 ? (uint32_t)(65535.999 * 32768.0) : (uint32_t)f;
    }
    r = mga_floor(x);
    if (r < 0) r = 0;
    if (r > 65535.999 * 32768.0) r = 65535.999 * 32768.0;
    return (uint32_t)r;
}

MGA_INLINE uint64_t z32_start(double z)
{
    double r, x = z * 32768.0;
    if (x >= 0.0 && x < 4611686018427387904.0) {               /* 2^62 */
        int64_t f = mga_ifloor64(x);
        return (double)f > 4294967295.999 * 32768.0 ? (uint64_t)(4294967295.999 * 32768.0) : (uint64_t)f;
    }
    r = mga_floor(x);
    if (r < 0) r = 0;
    if (r > 4294967295.999 * 32768.0) r = 4294967295.999 * 32768.0;
    return (uint64_t)r;
}

/* As the original: floor inside +-(2^31 - 1), truncation toward zero beyond
 * (mga_floor returns large values unchanged and the cast truncates). */
MGA_INLINE int64_t z32_inc(double d)
{
    double x = d * 32768.0 + 0.5;
    if (x > -2147483647.0 && x < 2147483647.0)
        return (int64_t)mga_ifloor(x);
    if (x > -4611686018427387904.0 && x < 4611686018427387904.0)
        return mga_itrunc64(x);
    return (int64_t)mga_floor(x);
}

#endif
