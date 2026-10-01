/* fpcheck.inc - checks of the float-to-integer helpers (mga/fp.h) and the
 * setup conversions (mga/setupconv.h), shared by the host test
 * (tests/unit/test_fp.c, built 32-bit x87 and 64-bit) and the DOS program
 * (tests/hal/fpcheck.c, Open Watcom and DJGPP, run in 86Box: the Watcom
 * #pragma aux forms are checked only there).
 *
 * The including file defines
 *   static int fpc_pass;     (set here: precision * 4 + rounding mode running)
 *   static int fpc_86box;    (1 in 86Box: its FISTP rounds tiny negatives to
 *                             +1 in round-to-nearest; mga_irint shows it, the
 *                             floor and truncation must not; counted in
 *                             fpc_quirks instead of failing. And its x87
 *                             arithmetic rounds in the guest's mode or the
 *                             host's (nearest) depending on whether the
 *                             recompiler has taken the code: the setup
 *                             conversions, which do arithmetic, are compared
 *                             in round-to-nearest only there, the mode
 *                             FPU_ENTER sets for every setup)
 *   static void fpc_fail(const char *what, double v, long long got, long long want);
 * and calls fpcheck_run(), which returns the number of cases checked.
 *
 * References need no libm: floor and truncation come from the casts the
 * helpers replace, the expected rounding of each mode from that floor,
 * worked out at the program's own precision before the mode is set. The
 * setup conversions are held against verbatim copies of the code they
 * replaced, run in the same mode: they must agree bit for bit. Every x87
 * rounding mode is checked at 53-bit precision (setup runs at 53-bit
 * inside FPU_ENTER), the helpers also at 24 and 64 bits. Afterwards the
 * FPU stack must be empty (an unbalanced FISTP form would leak a register
 * per call, and its results would go wrong within eight). */
#include "mga/fp.h"
#include "mga/setupconv.h"

/* ---- The code the helpers replaced (hal, before 2026-10-01) ---------- */
static double fpc_floor_old(double v)
{
    double t;
    if (v >= 2147483647.0 || v <= -2147483647.0)
        return v;
    t = (double)(int32_t)v;
    return t > v ? t - 1.0 : t;
}
static int32_t fpc_fx_old(double v, double scale)
{
    double r = fpc_floor_old(v * scale + 0.5);
    if (r > 2147483647.0) r = 2147483647.0;
    if (r < -2147483648.0) r = -2147483648.0;
    return (int32_t)r;
}
static uint32_t fpc_col_old(double v)
{
    double r = fpc_floor_old(v * 32768.0);
    if (r < 0) r = 0;
    if (r > 255.999 * 32768.0) r = 255.999 * 32768.0;
    return (uint32_t)r;
}
static uint32_t fpc_z16_old(double z)
{
    double r = fpc_floor_old(z * 32768.0);
    if (r < 0) r = 0;
    if (r > 65535.999 * 32768.0) r = 65535.999 * 32768.0;
    return (uint32_t)r;
}
static uint64_t fpc_z32_old(double z)
{
    double r = fpc_floor_old(z * 32768.0);
    if (r < 0) r = 0;
    if (r > 4294967295.999 * 32768.0) r = 4294967295.999 * 32768.0;
    return (uint64_t)r;
}
static int64_t fpc_z32inc_old(double d)
{
    return (int64_t)fpc_floor_old(d * 32768.0 + 0.5);
}

/* ---- FPU state ------------------------------------------------------- */
#if defined(MGA_FISTP) && defined(__WATCOMC__)
void fpc_stenv(void *p);
#  pragma aux fpc_stenv = "fnstenv [eax]" parm [eax] modify exact [];
#  define FPC_TAGS 1
#elif defined(MGA_FISTP) && defined(__GNUC__)
static void fpc_stenv(void *p) { __asm__ __volatile__("fnstenv (%0)" : : "r"(p) : "memory"); }
#  define FPC_TAGS 1
#endif

/* Mode switches the compiler may not move arithmetic across: GCC does not
 * model the control word, so FPU_ENTER's fldcw has no ordering against the
 * surrounding maths; here every switch is also a compiler memory barrier
 * and the inputs and results go through memory. */
#if defined(FPC_TAGS) && defined(__GNUC__)
static void fpc_set_cw(unsigned short cw) { __asm__ __volatile__("fldcw %0" : : "m"(cw) : "memory"); }
#elif defined(FPC_TAGS)
#  define fpc_set_cw fp_set_cw
#endif

#ifdef FPC_TAGS
/* The tag word (all 0xFFFF: every register empty); fnstenv masks the
 * exceptions, so the control word is put back. */
static unsigned fpc_tags(void)
{
    uint16_t env[14];
    unsigned short cw = fp_get_cw();
    fpc_stenv(env);
    fpc_set_cw(cw);
    return env[4];
}
#endif

static long fpc_quirks;

/* ---- Inputs ---------------------------------------------------------- */
#define FPC_N 6000
static double fpc_in[FPC_N];
static uint32_t fpc_seed = 0x2545F491u;
static uint32_t fpc_rnd(void)
{
    fpc_seed ^= fpc_seed << 13;
    fpc_seed ^= fpc_seed >> 17;
    fpc_seed ^= fpc_seed << 5;
    return fpc_seed;
}
static double fpc_pow2(int e)
{
    double p = 1.0;
    while (e > 0) { p *= 2.0; e--; }
    while (e < 0) { p *= 0.5; e++; }
    return p;
}
/* Values of every size, with the halves and the 2^31 edges the helpers
 * must get right, plus the non-finite ones only the guards see. */
static void fpc_make_inputs(void)
{
    static const double edge[] = { 0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994,
        -0.49999999999999994, 1.0, -1.0, 2147483646.5, -2147483646.5, 2147483646.0, -2147483646.0,
        2147483646.9999998, -2147483646.9999998, 2147483647.0, -2147483647.0, 2147483647.5, -2147483648.0,
        2147483648.0, -2147483649.0, 4294967296.5, 1e-310, -1e-310, 4503599627370495.5, -4503599627370495.5,
        4503599627370496.0, 9007199254740993.0, 4611686018427387903.0, -4611686018427387903.0,
        255.999, 65535.999, 8388575.5, 2147483615.5, 140737488322559.5, 140737488322560.5, 1e300, -1e300 };
    int i, n = (int)(sizeof edge / sizeof edge[0]);
    for (i = 0; i < n; i++)
        fpc_in[i] = edge[i];
    for (; i < FPC_N; i++) {
        double m = (double)(fpc_rnd() & 0xFFFFF) * 4294967296.0 + (double)fpc_rnd();    /* 52 bits */
        int e = (int)(fpc_rnd() % 96) - 60;
        double v = (m / 4503599627370496.0 + 1.0) * fpc_pow2(e);
        if (fpc_rnd() & 1) v = -v;
        if (i % 7 == 0) v = (double)((int32_t)fpc_rnd() >> (fpc_rnd() % 31)) + 0.5;    /* halves */
        fpc_in[i] = v;
    }
}

/* Floor from a 64-bit cast (exact for |v| < 2^62; larger doubles are
 * integers already), then each mode's rounding. */
static double fpc_floor_ref(double v)
{
    double t;
    if (!(v > -4611686018427387904.0 && v < 4611686018427387904.0))
        return v;
    t = (double)(int64_t)v;
    return t > v ? t - 1.0 : t;
}
static double fpc_ref(double v, int mode)
{
    double f = fpc_floor_ref(v), d = v - f;
    switch (mode) {
    case 0: return d > 0.5 ? f + 1 : d < 0.5 ? f : (fpc_floor_ref(f * 0.5) * 2.0 == f ? f : f + 1);
    case 1: return f;
    case 2: return d > 0 ? f + 1 : f;
    default: return v >= 0 ? f : (d > 0 ? f + 1 : f);
    }
}

static long fpcheck_run(void)
{
    static double want_rint[4][FPC_N], want_floor[FPC_N], want_trunc[FPC_N];
    static const unsigned short prec[3] = { 0x0200, 0x0000, 0x0300 };    /* 53, 24, 64 bits */
    static const double scales[3] = { 32768.0, 1048576.0, 65536.0 };
    long cases = 0;
    int i, m, p;
    unsigned short saved = 0;
    (void)saved;
    fpc_make_inputs();
    for (i = 0; i < FPC_N; i++) {
        double v = fpc_in[i];
        want_floor[i] = fpc_ref(v, 1);
        want_trunc[i] = fpc_ref(v, 3);
        for (m = 0; m < 4; m++)
            want_rint[m][i] = fpc_ref(v, m);
    }
#ifdef FPC_TAGS
    saved = fp_get_cw();
#endif
    for (p = 0; p < 3; p++)
        for (m = 0; m < 4; m++) {
            unsigned short cw = (unsigned short)(0x007F | prec[p] | (m << 10));
            (void)cw;
#ifdef FPC_TAGS
            fpc_set_cw(cw);
#else
            if (p || m)                 /* no x87 control word here: one pass */
                continue;
#endif
            fpc_pass = p * 4 + m;
            for (i = 0; i < FPC_N; i++) {
                double v = ((volatile double *)fpc_in)[i];
                const char *bad = NULL;
                long long got = 0, want = 0;
                if (v > -2147483647.0 && v < 2147483647.0) {
                    if ((double)mga_ifloor(v) != want_floor[i])
                        bad = "ifloor", got = mga_ifloor(v), want = (long long)want_floor[i];
                    else if ((double)mga_itrunc(v) != want_trunc[i])
                        bad = "itrunc", got = mga_itrunc(v), want = (long long)want_trunc[i];
                    else if (m == 0 && (double)mga_irint_nearest(v) != want_rint[0][i])
                        bad = "irint_nearest", got = mga_irint_nearest(v), want = (long long)want_rint[0][i];
                    else if ((double)mga_irint(v) != want_rint[m][i]) {
                        if (fpc_86box && m == 0 && v < 0 && v > -1.1102230246251565e-16 && mga_irint(v) == 1)
                            fpc_quirks++;
                        else
                            bad = "irint", got = mga_irint(v), want = (long long)want_rint[m][i];
                    }
                    else if (p == 0 && mga_floor(v) != fpc_floor_old(v))   /* 53-bit, as in FPU_ENTER */
                        bad = "mga_floor", got = (long long)mga_floor(v), want = (long long)fpc_floor_old(v);
                }
                if (!bad && v > -4611686018427387904.0 && v < 4611686018427387904.0) {
                    if ((double)mga_ifloor64(v) != want_floor[i])
                        bad = "ifloor64", got = mga_ifloor64(v), want = (long long)want_floor[i];
                    else if ((double)mga_itrunc64(v) != want_trunc[i])
                        bad = "itrunc64", got = mga_itrunc64(v), want = (long long)want_trunc[i];
                }
                if (!bad && p == 0 && (!fpc_86box || m == 0)) {
                    /* The setup conversions, against the code they replaced. */
                    double z = v * (1.0 / 4096.0);
                    int s;
                    for (s = 0; s < 3 && !bad; s++)
                        if (fx(v, scales[s]) != fpc_fx_old(v, scales[s]))
                            bad = "fx", got = fx(v, scales[s]), want = fpc_fx_old(v, scales[s]);
                    if (bad)
                        ;
                    else if (col_start(v) != fpc_col_old(v))
                        bad = "col_start", got = col_start(v), want = fpc_col_old(v);
                    else if (col_start(z) != fpc_col_old(z))
                        bad = "col_start(z)", got = col_start(z), want = fpc_col_old(z);
                    else if (z16_start(v) != fpc_z16_old(v))
                        bad = "z16_start", got = z16_start(v), want = fpc_z16_old(v);
                    else if (z32_start(v) != fpc_z32_old(v))
                        bad = "z32_start", got = (long long)z32_start(v), want = (long long)fpc_z32_old(v);
                    else if (z32_inc(v) != fpc_z32inc_old(v))
                        bad = "z32_inc", got = z32_inc(v), want = fpc_z32inc_old(v);
                    else if (z32_inc(z) != fpc_z32inc_old(z))
                        bad = "z32_inc(z)", got = z32_inc(z), want = fpc_z32inc_old(z);
                }
                cases++;
                if (bad) {
                    static volatile long long vgot, vwant;
                    vgot = got;
                    vwant = want;
#ifdef FPC_TAGS
                    fpc_set_cw(saved);
#endif
                    fpc_fail(bad, v, vgot, vwant);
#ifdef FPC_TAGS
                    fpc_set_cw(cw);
#endif
                }
            }
        }
#ifdef FPC_TAGS
    fpc_set_cw(saved);
    if (fpc_tags() != 0xFFFF)
        fpc_fail("fpu stack not empty", 0.0, (long long)fpc_tags(), 0xFFFF);
#endif
    {   /* NaN and infinities: only the guards and the old code see them. */
        volatile double zero = 0.0;
        double nan = zero / zero, inf = 1.0 / zero, odd[3];
        odd[0] = nan; odd[1] = inf; odd[2] = -inf;
        for (i = 0; i < 3; i++) {
            double v = odd[i];
            if (fx(v, 32768.0) != fpc_fx_old(v, 32768.0))
                fpc_fail("fx(nan/inf)", v, fx(v, 32768.0), fpc_fx_old(v, 32768.0));
            if (col_start(v) != fpc_col_old(v))
                fpc_fail("col_start(nan/inf)", v, col_start(v), fpc_col_old(v));
            if (z16_start(v) != fpc_z16_old(v))
                fpc_fail("z16_start(nan/inf)", v, z16_start(v), fpc_z16_old(v));
            cases++;
        }
    }
    return cases;
}
