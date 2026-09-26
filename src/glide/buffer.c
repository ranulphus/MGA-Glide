/* buffer.c - clears, swaps and render-buffer selection. */
#include "glide/mg.h"
#include "mga/sys.h"
#include "tex/texmgr.h"

GR_ENTRY(void, grRenderBuffer, (GrBuffer_t buffer))
{
    mg.render_buffer = buffer == GR_BUFFER_FRONTBUFFER ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER;
    mg.dirty |= MG_DIRTY_TARGET;
}

GR_ENTRY(void, grBufferClear, (GrColor_t color, GrAlpha_t alpha, FxU16 depth))
{
    int x0 = mg.st.clip_x0, x1 = mg.st.clip_x1, y0 = mg.st.clip_y0, y1 = mg.st.clip_y1;
    MGA_UNUSED(alpha);
    if (!mg.open)
        return;
    mg_validate();
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT) {
        y0 = mg.height - mg.st.clip_y1;
        y1 = mg.height - mg.st.clip_y0;
    }
    if (mg.st.color_mask_rgb)
        engine_fill(x0, y0, x1 - x0, y1 - y0, mg_argb_to_565(mg_color_to_argb(color)));
    if (mg.has_aux && mg.st.depth_mask && mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE)
        engine_fill_depth(x0, y0, x1 - x0, y1 - y0, mg_depth_clear_value(depth));
}

GR_ENTRY(void, grBufferSwap, (int swap_interval))
{
    int i, t;
    if (!mg.open)
        return;
    engine_sync(200000);
    for (i = 0; i < swap_interval; i++)
        engine_vsync_wait(100000);
    vbe_set_display_start(mg.buf_off[mg.back], mg.pitch_px * 2, 16);
    t = mg.front;
    mg.front = mg.back;
    mg.back = (mg.nbuffers == 3) ? (3 - mg.front - t) : t;
    if (mg.nbuffers == 3 && mg.back == mg.front)
        mg.back = (mg.front + 1) % 3;
    mg.dirty |= MG_DIRTY_TARGET;
    mg.frame++;
    tex_frame();
    mg_frame_end();
}

GR_ENTRY(int, grBufferNumPending, (void)) { return 0; }

/* exp and log without libm, for the gamma ramp. */
static double g_exp(double x)
{
    double r = 1.0, term = 1.0;
    int i, k = 0;
    while (x > 1.0) { x *= 0.5; k++; }
    while (x < -1.0) { x *= 0.5; k++; }
    for (i = 1; i < 20; i++) { term *= x / i; r += term; }
    while (k--) r *= r;
    return r;
}

static double g_ln(double v)
{
    double z, z2, ln;
    int e = 0;
    while (v >= 2.0) { v *= 0.5; e++; }
    while (v < 1.0) { v *= 2.0; e--; }
    z = (v - 1.0) / (v + 1.0);
    z2 = z * z;
    ln = 2.0 * z * (1.0 + z2 * (1.0 / 3 + z2 * (1.0 / 5 + z2 * (1.0 / 7 + z2 / 9))));
    return ln + e * 0.6931471805599453;
}

/* Load the palette ramp for mg.gamma. In direct-colour modes each pixel
 * component indexes the palette RAM, and the video BIOS leaves whatever it
 * likes there, so the ramp is loaded at every window open, not only when
 * the game asks for gamma. */
void mg_gamma_apply(void)
{
    uint8_t ramp[256];
    double g = mg_config.gamma_enable ? mg.gamma : 1.0;
    int i;
    for (i = 0; i < 256; i++) {
        double x = i / 255.0, y = i ? g_exp(g_ln(x) / g) : 0.0;
        int v = (int)(y * 255.0 + 0.5);
        ramp[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    dac_set_ramp(ramp);
}

GR_ENTRY(void, grGammaCorrectionValue, (float value))
{
    FPU_ENTER();
    if (value <= 0.0f)
        value = 1.0f;
    mg.gamma = value;
    if (mg.open)
        mg_gamma_apply();
    FPU_LEAVE();
}
