/* vbe.c - mode setting through the card's VBE BIOS (PRD §7.7). */
#include "mga/hal.h"
#include "mga/sys.h"
#include <string.h>

static uint8_t *vbe_buf;
static uint16_t vbe_seg;
static int vbe_ver;

static int vbe_call(sys_rmregs *r)
{
    if (sys_rm_int(0x10, r) < 0)
        return -1;
    return (r->eax & 0xFFFF) == 0x004F ? 0 : -1;
}

static int vbe_init(void)
{
    sys_rmregs r;
    if (vbe_buf)
        return 0;
    vbe_buf = (uint8_t *)sys_dos_alloc(1024, &vbe_seg);
    if (!vbe_buf)
        return -1;
    memset(vbe_buf, 0, 512);
    memcpy(vbe_buf, "VBE2", 4);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F00;
    r.es = vbe_seg;
    r.edi = 0;
    if (vbe_call(&r) < 0 || memcmp(vbe_buf, "VESA", 4) != 0)
        return -1;
    vbe_ver = vbe_buf[4] | (vbe_buf[5] << 8);
    return 0;
}

int vbe_version(void) { return vbe_init() == 0 ? vbe_ver : 0; }

/* Video memory the BIOS reports (VBE info block TotalMemory, 64 KB units):
 * safe before any mode is set, unlike mga_probe_vram(), which writes VRAM. */
uint32_t vbe_total_memory(void)
{
    if (vbe_init() < 0)
        return 0;
    return (uint32_t)(vbe_buf[0x12] | (vbe_buf[0x13] << 8)) << 16;
}

static int mode_info(uint16_t mode, mga_vbe_mode *m)
{
    sys_rmregs r;
    uint8_t *b = vbe_buf + 512;
    memset(b, 0, 256);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F01;
    r.ecx = mode;
    r.es = vbe_seg;
    r.edi = 512;
    if (vbe_call(&r) < 0)
        return -1;
    memset(m, 0, sizeof *m);
    m->mode = mode;
    m->pitch_bytes = (uint16_t)(b[0x10] | (b[0x11] << 8));
    m->width = (uint16_t)(b[0x12] | (b[0x13] << 8));
    m->height = (uint16_t)(b[0x14] | (b[0x15] << 8));
    m->bpp = b[0x19];
    m->red_size = b[0x1F]; m->red_pos = b[0x20];
    m->green_size = b[0x21]; m->green_pos = b[0x22];
    m->blue_size = b[0x23]; m->blue_pos = b[0x24];
    m->lfb_phys = (uint32_t)b[0x28] | ((uint32_t)b[0x29] << 8) | ((uint32_t)b[0x2A] << 16) | ((uint32_t)b[0x2B] << 24);
    /* Usable: supported, graphics, linear framebuffer, direct colour. */
    if ((b[0] & 0x91) != 0x91 || b[0x1B] != 6)
        return 1;
    return 0;
}

int vbe_enumerate(void (*cb)(const mga_vbe_mode *m, void *ctx), void *ctx)
{
    uint16_t list[128], *p;
    int n = 0, i;
    if (vbe_init() < 0)
        return -1;
    p = (uint16_t *)sys_real_ptr((uint16_t)(vbe_buf[0x10] | (vbe_buf[0x11] << 8)),
                                 (uint16_t)(vbe_buf[0x0E] | (vbe_buf[0x0F] << 8)));
    while (p && *p != 0xFFFF && n < 128)
        list[n++] = *p++;
    for (i = 0; i < n; i++) {
        mga_vbe_mode m;
        if (mode_info(list[i], &m) == 0)
            cb(&m, ctx);
    }
    return n;
}

typedef struct { int w, h, bpp; mga_vbe_mode best; int found; } find_ctx;

static void find_cb(const mga_vbe_mode *m, void *ctx)
{
    find_ctx *f = (find_ctx *)ctx;
    if (m->width != f->w || m->height != f->h || m->bpp != f->bpp || f->found)
        return;
    if (f->bpp == 16 && !(m->red_size == 5 && m->green_size == 6 && m->blue_size == 5))
        return;
    f->best = *m;
    f->found = 1;
}

int vbe_find_mode(int w, int h, int bpp, mga_vbe_mode *out)
{
    find_ctx f;
    memset(&f, 0, sizeof f);
    f.w = w; f.h = h; f.bpp = bpp;
    if (vbe_enumerate(find_cb, &f) < 0 || !f.found)
        return -1;
    *out = f.best;
    return 0;
}

int vbe_set_mode(const mga_vbe_mode *m, int pitch_px, int *actual_pitch_px)
{
    sys_rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = 0x4000u | m->mode;          /* linear framebuffer */
    if (vbe_call(&r) < 0)
        return -1;
    *actual_pitch_px = m->pitch_bytes / (m->bpp / 8);
    if (pitch_px > 0) {
        memset(&r, 0, sizeof r);
        r.eax = 0x4F06;
        r.ebx = 0;                       /* set scan line length in pixels */
        r.ecx = (uint32_t)pitch_px;
        if (vbe_call(&r) == 0)
            *actual_pitch_px = (int)((r.ebx & 0xFFFF) / (uint32_t)(m->bpp / 8));
    }
    return 0;
}

int vbe_set_display_start(uint32_t byte_offset, int pitch_bytes, int bpp)
{
    sys_rmregs r;
    uint32_t bytes_pp = (uint32_t)bpp / 8;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F07;
    r.ebx = 0x0000;
    r.ecx = (byte_offset % (uint32_t)pitch_bytes) / bytes_pp;
    r.edx = byte_offset / (uint32_t)pitch_bytes;
    return vbe_call(&r);
}

void vbe_set_text_mode(void)
{
    sys_rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    sys_rm_int(0x10, &r);
}
