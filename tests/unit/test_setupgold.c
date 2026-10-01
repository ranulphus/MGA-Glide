/* Setup goldens: the drawing engine's register state at every start of a
 * draw (a write through a +0x100 start alias), hashed, for fixed scenarios
 * that exercise setup_triangle with every flag combination, both edge rules,
 * 32-bit Z, G400 dual texturing, tex_adjust_coords, extreme and NaN inputs
 * and clip rows, with fills, TLUT loads, ILOADs, presents, texture state and
 * engine_init in between.
 *
 * The register state is what the engine sees, not the write stream: the
 * last value written to every drawing register, with the G400's texture
 * registers kept per map and routed as the chip does (tmap0dis). So a
 * change that drops writes of values already in place, or regroups FIFO
 * reservations, keeps the hashes, while any change to a value reaching a
 * draw does not. tests/unit/data/setupgold-<abi>.txt holds the hashes made
 * before the triangle-path performance work (2026-10-01).
 *
 * SETUPGOLD_UPDATE=1 rewrites the file. SETUPGOLD_DUMP=<scenario> prints the
 * hash of every draw of that scenario (to bisect a difference). */
#include "unit.h"
#include "refrast.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "mga/tex.h"
#include "mga/fp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

mga_chip mga;
volatile uint8_t *mga_mmio, *mga_fb;
void fifo_reserve(int n) { (void)n; }
void fifo_reset(void) {}
uint32_t sys_time_us(void) { static uint32_t t; return t += 10; }
void sys_delay_us(uint32_t us) { (void)us; }

#define NREG (0x4000 / 4)
static uint32_t plain[NREG], bank[2][NREG];
static int m1_tc2, m1_tw, m1_th;
static uint64_t run_hash;
static uint32_t draws;
static int dumping;

static uint64_t fnv(uint64_t h, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 0x100000001B3ull;
    }
    return h;
}

/* The G400's per-map texture registers (86Box patch 0009, g400_map_reg). */
static int banked(uint32_t a)
{
    if (a >= MGAREG_TMR0 && a <= MGAREG_TMR(8))
        return 1;
    if (a >= MGAREG_TEXORG1 && a <= MGAREG_TEXORG1 + 12)
        return 1;
    switch (a) {
    case MGAREG_TEXORG: case MGAREG_TEXWIDTH: case MGAREG_TEXHEIGHT: case MGAREG_TEXCTL:
    case MGAREG_TEXTRANS: case MGAREG_TEXTRANSHIGH: case MGAREG_TEXCTL2: case MGAREG_TEXFILTER:
    case MGAREG_TEXBORDERCOL: case MGAREG_ALPHACTRL:
        return 1;
    default:
        return 0;
    }
}

static void state_hash(uint32_t a, uint32_t v)
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t r;
    h = fnv(h, a);
    h = fnv(h, v);
    for (r = 0x1C00; r < 0x1D00; r += 4)
        h = fnv(h, plain[r >> 2]);
    for (r = 0x2C00; r < 0x2D00; r += 4) {
        if (mga.has_dual_tex && banked(r)) {
            h = fnv(h, bank[0][r >> 2]);
            h = fnv(h, bank[1][r >> 2]);
        } else
            h = fnv(h, plain[r >> 2]);
    }
    draws++;
    run_hash = fnv(fnv(run_hash, (uint32_t)h), (uint32_t)(h >> 32));
    if (dumping)
        printf("  draw %u reg %04x val %08x hash %016llx\n", draws, a, v, (unsigned long long)h);
}

static void hook(uint32_t off, uint32_t v)
{
    int exec = ((off >= 0x1C00 && off < 0x1E00) || (off >= 0x2C00 && off < 0x2E00)) && (off & 0x300) == 0x100;
    uint32_t a = exec ? off & ~0x100u : off;
    if (a >= 0x4000)
        return;
    if (mga.has_dual_tex && banked(a)) {
        /* tmap0dis takes effect from the next write (G400 spec p.3-216). */
        bank[1][a >> 2] = v;
        if (!(m1_tc2 | m1_tw | m1_th))
            bank[0][a >> 2] = v;
        if (a == MGAREG_TEXCTL2)
            m1_tc2 = (int)(v >> 31);
        else if (a == MGAREG_TEXWIDTH)
            m1_tw = (int)(v >> 31);
        else if (a == MGAREG_TEXHEIGHT)
            m1_th = (int)(v >> 31);
    } else
        plain[a >> 2] = v;
    if (exec)
        state_hash(a, v);
}

/* xorshift32: the same sequence on every host. */
static uint32_t seed;
static uint32_t rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
static int rndi(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
static double rndf(double lo, double hi) { return lo + (hi - lo) * (rnd() / 4294967296.0); }
static int chance(int pct) { return (int)(rnd() % 100) < pct; }

enum { W = 640, H = 480 };

static double odd_value(void)
{
    switch (rndi(0, 5)) {
    case 0: return NAN;
    case 1: return INFINITY;
    case 2: return -INFINITY;
    case 3: return rndf(-1e9, 1e9);
    case 4: return 65535.999;
    default: return -rndf(0, 300);
    }
}

static void rnd_vtx(mga_svtx *v, int extreme, int z32)
{
    int span = extreme ? 4096 : 64;
    memset(v, 0, sizeof *v);
    v->X16 = rndi(-span * 16, (W + span) * 16);
    v->Y16 = rndi(-span * 16, (H + span) * 16);
    v->z = z32 ? rndf(0, 4294967295.0) : rndf(0, 65535.0);
    v->r = (float)rndf(0, 255); v->g = (float)rndf(0, 255); v->b = (float)rndf(0, 255);
    v->a = (float)rndf(0, 255); v->fog = (float)rndf(0, 255);
    v->sr = (float)rndf(0, 255); v->sg = (float)rndf(0, 255); v->sb = (float)rndf(0, 255);
    v->q = (float)rndf(0.02, 2.0);
    v->s = (float)(rndf(-4, 4) * v->q); v->t = (float)(rndf(-4, 4) * v->q);
    v->s1 = (float)(rndf(-16, 16) * v->q); v->t1 = (float)(rndf(-16, 16) * v->q);
    if (chance(10)) { v->r = (float)rndf(-60, 320); v->g = (float)rndf(-60, 320); }
    if (chance(5)) v->z = z32 ? rndf(4294967200.0, 4294967296.5) : rndf(65534.9, 65536.5);
    if (chance(5)) v->z = -rndf(0, 10);
    if (extreme) {
        if (chance(20)) v->z = odd_value();
        if (chance(20)) v->r = (float)odd_value();
        if (chance(10)) v->fog = (float)odd_value();
        if (chance(20)) v->q = (float)(chance(50) ? rndf(1e-7, 1e-4) : rndf(10, 5000));
        if (chance(15)) v->s = (float)odd_value();
        if (chance(10)) v->t1 = (float)rndf(-1e6, 1e6);
    }
}

static void rnd_tex(mga_texstate *t)
{
    int k;
    memset(t, 0, sizeof *t);
    t->org = (uint32_t)rndi(0, 0x3FFF) << 6;
    t->mip_n = mga.max_mip_levels > 1 && chance(40) ? rndi(2, 5) : 0;
    for (k = 0; k < 5; k++)
        t->mip_org[k] = t->org + (uint32_t)k * 0x1000;
    t->w_log2 = rndi(3, 10);
    t->h_log2 = rndi(3, 10);
    t->pitch = 1 << t->w_log2;
    t->hwfmt = (uint32_t)rndi(0, 2) == 0 ? TEXCTL_TW16 : TEXCTL_TW15;
    t->clamp_u = chance(30);
    t->clamp_v = chance(30);
    t->modulate = chance(70);
    t->bilinear = chance(60);
    t->trilinear = t->mip_n > 1 && chance(40);
    t->key_alpha0 = chance(20);
    t->texctl2 = mga.has_texctl2 && chance(20) ? TEXCTL2_DECALBLEND : 0;
}

static mga_texstate ts0, ts1;
static mga_target tgt;

static void rnd_target(void)
{
    tgt.color_off = (uint32_t)rndi(0, 15) << 16;
    tgt.pitch_px = chance(50) ? 1024 : 640;
    tgt.bpp = 16;
    tgt.zbits = chance(80) ? 16 : 32;
    tgt.z_off = chance(90) ? 0x400000u + ((uint32_t)rndi(0, 7) << 16) : 0;
    engine_set_target(&tgt);
}

static void interleave(void)
{
    switch (rndi(0, 9)) {
    case 0:
        engine_fill(rndi(0, W - 1), rndi(0, H - 1), rndi(1, 64), rndi(1, 64), rnd() & 0xFFFF);
        break;
    case 1:
        engine_fill_depth(rndi(0, W - 1), rndi(0, H - 1), rndi(1, 64), rndi(1, 64), rnd() & 0xFFFF);
        break;
    case 2:
        engine_set_clip(rndi(0, 100), rndi(0, 100), rndi(200, W), rndi(200, H));
        break;
    case 3:
        engine_set_maccess_flags((chance(50) ? MACCESS_FOGEN : 0) | (chance(50) ? MACCESS_NODITHER : 0));
        break;
    case 4:
        if (mga.has_tlut)
            engine_tlut_load((uint32_t)rndi(0, 0x1FFF) << 6, rndi(0, 128), rndi(1, 128));
        break;
    case 5:
        if (mga.has_dstorg) {
            static uint32_t row[64];
            int w = rndi(1, 64), h = rndi(1, 8), n, j;
            n = engine_iload_begin((uint32_t)rndi(0, 0x1FFF) << 6, 256, 16, rndi(0, 255 - w), rndi(0, 200), w, h);
            for (j = 0; n && j < h; j++) {
                row[0] = rnd();
                engine_iload_row(row);
            }
            if (n)
                engine_iload_end();
        }
        break;
    case 6:
        rnd_tex(&ts0);
        if (mga.has_dual_tex && chance(50)) {
            rnd_tex(&ts1);
            tex_emit_dual(&ts0, &ts1);
        } else
            tex_emit(&ts0);
        if (mga.has_dual_tex)
            tex_emit_combiner(rnd(), chance(50) ? rnd() : 0);
        break;
    case 7:
        if (mga.has_texctl2) {
            mga_surface src = { 0x400000, 320, 240, 512 }, dst = { 0x200000, 640, 480, 1024 };
            if (mga_present_ok(&src))
                engine_present(&src, &dst, rndi(0, 8), rndi(0, 8), rndi(600, 640), rndi(440, 480),
                               chance(50) ? MGA_PRESENT_BILINEAR : MGA_PRESENT_NEAREST);
        }
        break;
    case 8:
        rnd_target();
        break;
    default:
        if (chance(10)) {
            engine_init(rndi(0, 1) ? 1024 : 640, 16);
            rnd_target();
        }
        break;
    }
}

static void scenario(const char *name, mga_family fam, int voodoo, int extreme, int n, FILE *out,
                     const char *want_file)
{
    int i;
    const char *dump = getenv("SETUPGOLD_DUMP");
    /* As in the drivers, whose entry points run setup inside FPU_ENTER
     * (53-bit, round to nearest): the x87 build's default 64-bit precision
     * would make results depend on which values the compiler spills. */
    FPU_ENTER();
    memset(&mga, 0, sizeof mga);
    mga.family = fam;
    mga_chip_caps(&mga);
    refrast_init();
    refrast_write_hook = hook;
    memset(plain, 0, sizeof plain);
    memset(bank, 0, sizeof bank);
    m1_tc2 = m1_tw = m1_th = 0;
    run_hash = 0xCBF29CE484222325ull;
    draws = 0;
    dumping = dump && !strcmp(dump, name);
    seed = 0x9E3779B9u ^ (uint32_t)(fam * 7919 + voodoo * 104729 + extreme * 1299709);
    engine_init(1024, 16);
    rnd_target();
    engine_set_clip(0, 0, W, H);
    rnd_tex(&ts0);
    rnd_tex(&ts1);
    for (i = 0; i < n; i++) {
        mga_svtx a, b, c;
        mga_tri_ctx ctx;
        uint32_t f = 0;
        int z32;
        if (chance(4))
            interleave();
        memset(&ctx, 0, sizeof ctx);
        if (chance(85)) f |= MGA_S_COLOR;
        if (chance(80)) f |= MGA_S_Z;
        z32 = (f & MGA_S_Z) && chance(15);
        if (z32) f |= MGA_S_Z32;
        if (chance(30)) f |= MGA_S_ALPHA;
        if (chance(25)) f |= MGA_S_FOG;
        if (chance(70)) f |= MGA_S_TEX;
        if ((f & MGA_S_TEX) && chance(5)) f |= MGA_S_AFFINE;
        if (mga.has_specular && chance(10)) f |= MGA_S_SPEC;
        if (mga.has_dual_tex && (f & MGA_S_TEX) && chance(40)) f |= MGA_S_TEX2;
        if (voodoo) f |= MGA_S_VOODOO_EDGES;
        ctx.flags = f;
        ctx.dwgctl = ((f & MGA_S_TEX) ? DWG_OPCOD_TEXTURE_TRAP : DWG_OPCOD_TRAP) | DWG_BOP_COPY |
                     ((uint32_t)rndi(0, 7) << 8) | (chance(50) ? DWG_ATYPE_ZI : DWG_ATYPE_I);
        ctx.clip_y0 = chance(80) ? 0 : rndi(0, H / 2);
        ctx.clip_y1 = chance(80) ? H : rndi(H / 2, H);
        ctx.tex_tw = ts0.w_log2; ctx.tex_th = ts0.h_log2;
        ctx.tex_tw1 = ts1.w_log2; ctx.tex_th1 = ts1.h_log2;
        ctx.texctl2_1 = ts1.texctl2 | TEXCTL2_DUALTEX;
        rnd_vtx(&a, extreme, z32);
        rnd_vtx(&b, extreme, z32);
        rnd_vtx(&c, extreme, z32);
        if (chance(5)) b = a;                                   /* degenerate */
        if (chance(5)) { c.Y16 = a.Y16; }                       /* flat edge */
        if (chance(5)) { b.X16 = a.X16 + rndi(-2, 2); b.Y16 = a.Y16 + rndi(-2, 2); }   /* sliver */
        if (f & MGA_S_TEX)
            tex_adjust_coords(&a, &b, &c, &ts0);
        if (f & MGA_S_TEX2)
            tex_adjust_coords1(&a, &b, &c, &ts1);
        setup_triangle(&a, &b, &c, &ctx);
    }
    refrast_write_hook = NULL;
    FPU_LEAVE();
    if (out)
        fprintf(out, "%s %u %016llx\n", name, draws, (unsigned long long)run_hash);
    else {
        char line[256], want[64];
        snprintf(want, sizeof want, "%s %u %016llx", name, draws, (unsigned long long)run_hash);
        FILE *f = fopen(want_file, "r");
        int found = 0;
        while (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, name, strlen(name)) && line[strlen(name)] == ' ') {
                found = 1;
                if (strcmp(line, want))
                    fprintf(stderr, "setupgold: %s: got \"%s\", golden \"%s\"\n", name, want, line);
                CHECK(!strcmp(line, want));
            }
        }
        if (f)
            fclose(f);
        if (!found)
            fprintf(stderr, "setupgold: %s: no golden in %s (SETUPGOLD_UPDATE=1 makes one)\n", name, want_file);
        CHECK(found);
    }
}

int unit_main(void)
{
    static const struct { const char *name; mga_family fam; int voodoo, extreme; } sc[] = {
        { "g100-exact", MGA_FAMILY_G100, 0, 0 }, { "g100-voodoo", MGA_FAMILY_G100, 1, 0 },
        { "g200-exact", MGA_FAMILY_G200, 0, 0 }, { "g200-voodoo", MGA_FAMILY_G200, 1, 0 },
        { "g400-exact", MGA_FAMILY_G400, 0, 0 }, { "g400-voodoo", MGA_FAMILY_G400, 1, 0 },
        { "g200-extreme", MGA_FAMILY_G200, 0, 1 }, { "g400-extreme", MGA_FAMILY_G400, 1, 1 },
    };
    const char *file = sizeof(void *) == 4 ? "tests/unit/data/setupgold-host32.txt"
                                           : "tests/unit/data/setupgold-host64.txt";
    const char *upd = getenv("SETUPGOLD_UPDATE");
    FILE *out = NULL;
    size_t i;
    if (upd && *upd == '1') {
        out = fopen(file, "w");
        CHECK(out != NULL);
        if (!out)
            return 0;
        fprintf(out, "# tests/unit/test_setupgold.c: scenario, draws, hash of the register state at each draw\n");
    }
    for (i = 0; i < sizeof sc / sizeof sc[0]; i++)
        scenario(sc[i].name, sc[i].fam, sc[i].voodoo, sc[i].extreme, 4000, out, file);
    if (out) {
        fclose(out);
        printf("setupgold: wrote %s\n", file);
    }
    return 0;
}
