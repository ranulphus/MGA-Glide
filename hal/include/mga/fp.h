/* fp.h - x87 control-word handling for code running inside a game.
 *
 * Games often run the FPU at 24-bit precision and may change rounding.
 * Entry points that do setup maths switch to round-to-nearest, 53-bit,
 * all exceptions masked, and restore the game's control word on exit. */
#ifndef MGA_FP_H
#define MGA_FP_H
#include "mga/types.h"

#if defined(__WATCOMC__) && defined(__386__)
unsigned short fp_get_cw(void);
#pragma aux fp_get_cw = \
    "sub esp, 4" \
    "fnstcw [esp]" \
    "movzx eax, word ptr [esp]" \
    "add esp, 4" \
    value [ax];
void fp_set_cw(unsigned short cw);
#pragma aux fp_set_cw = \
    "push eax" \
    "fldcw [esp]" \
    "pop eax" \
    parm [ax];
#  define FPU_ENTER() unsigned short fp_saved_cw_ = fp_get_cw(); fp_set_cw(0x027F)
#  define FPU_LEAVE() fp_set_cw(fp_saved_cw_)
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
MGA_INLINE unsigned short fp_get_cw(void) { unsigned short cw; __asm__ __volatile__("fnstcw %0" : "=m"(cw)); return cw; }
MGA_INLINE void fp_set_cw(unsigned short cw) { __asm__ __volatile__("fldcw %0" : : "m"(cw)); }
#  define FPU_ENTER() unsigned short fp_saved_cw_ = fp_get_cw(); fp_set_cw(0x027F)
#  define FPU_LEAVE() fp_set_cw(fp_saved_cw_)
#else
#  define FPU_ENTER() ((void)0)
#  define FPU_LEAVE() ((void)0)
#endif

/* Float to integer without changing the control word. A C cast truncates,
 * so on the x87 the compiler wraps each FISTP in two FLDCWs, which stall a
 * P6 (a cast cost 35 emulated cycles, setup's fx() 91). FISTP alone rounds
 * in the current mode and lands on floor(v) or ceil(v), so one comparison
 * makes floor or truncation exact in any rounding and precision mode.
 *   mga_irint(v)    v rounded in the current mode (what lrint() does)
 *   mga_ifloor(v)   floor(v), exactly
 *   mga_itrunc(v)   v truncated toward zero, exactly
 * valid for -2^31+1 < v < 2^31-1 (callers check; NaN fails such a check);
 * the 64-bit forms for |v| < 2^62. 86Box's FISTP rounds tiny negatives
 * (|v| < 2^-53) to +1 in round-to-nearest (x87_fround: floor(v + 1.0) is
 * 1), so the floor steps down twice if it has to; real x87s never need the
 * second step. Exact in every mode means the same
 * results as the casts they replace: tests/unit/test_fp.c (host, x87) and
 * tests/hal/fpcheck.c (Watcom and DJGPP, in 86Box) check it. */
#if defined(__GNUC__) && defined(__i386__)
MGA_INLINE int32_t mga_irint(double v)
{
    int32_t r;
    __asm__ __volatile__("fistpl %0" : "=m"(r) : "t"(v) : "st");
    return r;
}
MGA_INLINE int64_t mga_irint64(double v)
{
    int64_t r;
    __asm__ __volatile__("fistpll %0" : "=m"(r) : "t"(v) : "st");
    return r;
}
#  define MGA_FISTP 1
#elif defined(__WATCOMC__) && defined(__386__)
int32_t mga_irint(double v);
#pragma aux mga_irint = \
    "sub esp, 4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax 8087];
int64_t mga_irint64(double v);
#pragma aux mga_irint64 = \
    "sub esp, 8" \
    "fistp qword ptr [esp]" \
    "pop eax" \
    "pop edx" \
    parm [8087] value [edx eax] modify exact [eax edx 8087];
#  define MGA_FISTP 1
#endif

#ifdef MGA_FISTP
MGA_INLINE int32_t mga_ifloor(double v)
{
    int32_t r = mga_irint(v);
    if ((double)r > v) {
        r--;
        if ((double)r > v)
            r--;
    }
    return r;
}
MGA_INLINE int32_t mga_itrunc(double v)
{
    int32_t r = mga_ifloor(v);
    return v < 0 && (double)r != v ? r + 1 : r;
}
MGA_INLINE int64_t mga_ifloor64(double v)
{
    int64_t r = mga_irint64(v);
    if ((double)r > v) {
        r--;
        if ((double)r > v)
            r--;
    }
    return r;
}
MGA_INLINE int64_t mga_itrunc64(double v)
{
    int64_t r = mga_ifloor64(v);
    return v < 0 && (double)r != v ? r + 1 : r;
}
#else
/* Other targets (the x86-64 host build): the plain conversions. */
MGA_INLINE int32_t mga_itrunc(double v) { return (int32_t)v; }
MGA_INLINE int32_t mga_ifloor(double v) { int32_t r = (int32_t)v; return (double)r > v ? r - 1 : r; }
MGA_INLINE int64_t mga_itrunc64(double v) { return (int64_t)v; }
MGA_INLINE int64_t mga_ifloor64(double v) { int64_t r = (int64_t)v; return (double)r > v ? r - 1 : r; }
#  if defined(__GNUC__)
MGA_INLINE int32_t mga_irint(double v) { return (int32_t)__builtin_lrint(v); }
MGA_INLINE int64_t mga_irint64(double v) { return (int64_t)__builtin_llrint(v); }
#  endif
#endif

/* lrint() for code inside FPU_ENTER (round to nearest): FISTP, corrected
 * where 86Box's rounds a tiny negative to +1 (above). On an x87 in that
 * mode the result is never more than half above v, so the step never
 * fires; DJGPP's lrint() gets the 86Box case right, and so must this. */
MGA_INLINE int32_t mga_irint_nearest(double v)
{
    int32_t r = mga_irint(v);
    return (double)r - v > 0.5 ? r - 1 : r;
}

/* floor() without the C maths library (the DLL links no libm). Exact for
 * |v| < 2^31; larger magnitudes are returned unchanged (already integral
 * at double precision for our uses, or clamped by the caller). */
MGA_INLINE double mga_floor(double v)
{
    double t;
    if (v > -2147483647.0 && v < 2147483647.0)
        return (double)mga_ifloor(v);
    if (v >= 2147483647.0 || v <= -2147483647.0)
        return v;
    t = (double)(int32_t)v;             /* NaN */
    return t > v ? t - 1.0 : t;
}

#endif
