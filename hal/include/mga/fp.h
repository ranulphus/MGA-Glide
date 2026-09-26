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

/* floor() without the C maths library (the DLL links no libm). Exact for
 * |v| < 2^31; larger magnitudes are returned unchanged (already integral
 * at double precision for our uses, or clamped by the caller). */
MGA_INLINE double mga_floor(double v)
{
    double t;
    if (v >= 2147483647.0 || v <= -2147483647.0)
        return v;
    t = (double)(int32_t)v;
    return t > v ? t - 1.0 : t;
}

#endif
