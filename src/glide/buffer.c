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
