/* vram.c - the card's memory shared out (GLOS M7, PRD Appendix B): the
 * front buffer, the cursor's image, windows kept off screen, and in time
 * DOS-GL's buffers. One table of blocks in address order, free or owned,
 * that covers [base, end) without gaps; the allocator keeps no memory of
 * its own, so any port (a kernel too) can use it. */
#include "mga/hal.h"

static void cut(mga_vram *v, int i, uint32_t at)        /* block i becomes [off, at) and [at, end) */
{
    int k;
    for (k = v->n; k > i + 1; k--)
        v->b[k] = v->b[k - 1];
    v->b[i + 1].off = at;
    v->b[i + 1].size = v->b[i].off + v->b[i].size - at;
    v->b[i + 1].owner = v->b[i].owner;
    v->b[i].size = at - v->b[i].off;
    v->n++;
}

static void join(mga_vram *v, int i)                    /* block i takes in block i + 1 */
{
    int k;
    v->b[i].size += v->b[i + 1].size;
    for (k = i + 1; k < v->n - 1; k++)
        v->b[k] = v->b[k + 1];
    v->n--;
}

/* Merge block i with free neighbours (it is free itself). */
static void settle(mga_vram *v, int i)
{
    if (i + 1 < v->n && !v->b[i + 1].owner)
        join(v, i);
    if (i > 0 && !v->b[i - 1].owner)
        join(v, i - 1);
}

void vram_init(mga_vram *v, uint32_t base, uint32_t end)
{
    v->base = base;
    v->end = end > base ? end : base;
    v->n = 1;
    v->b[0].off = base;
    v->b[0].size = v->end - base;
    v->b[0].owner = 0;
}

/* Take [off, off + bytes) out of the free blocks for owner; 0, or -1 when part of it is not free. */
static int take(mga_vram *v, int i, uint32_t off, uint32_t bytes, uint32_t owner)
{
    int need = (off > v->b[i].off) + (off + bytes < v->b[i].off + v->b[i].size);
    if (v->n + need > MGA_VRAM_BLOCKS)
        return -1;
    if (off > v->b[i].off) {
        cut(v, i, off);
        i++;
    }
    if (off + bytes < v->b[i].off + v->b[i].size)
        cut(v, i, off + bytes);
    v->b[i].owner = owner;
    return 0;
}

int vram_alloc(mga_vram *v, uint32_t bytes, uint32_t align, unsigned flags, uint32_t owner, uint32_t *off)
{
    int i, best = -1;
    uint32_t at = 0, best_at = 0;
    if (!bytes || !owner || (align & (align - 1)))
        return -1;
    if (!align)
        align = 1;
    for (i = 0; i < v->n; i++) {
        uint32_t lo = v->b[i].off, hi = lo + v->b[i].size;
        if (v->b[i].owner || v->b[i].size < bytes)
            continue;
        if (flags & MGA_VRAM_TOP)
            at = (hi - bytes) & ~(align - 1);
        else
            at = (lo + align - 1) & ~(align - 1);
        if (at < lo || at + bytes > hi || at + bytes < at)
            continue;
        best = i;
        best_at = at;
        if (!(flags & MGA_VRAM_TOP))
            break;                              /* first fit from the bottom; from the top, the last one */
    }
    if (best < 0 || take(v, best, best_at, bytes, owner) != 0)
        return -1;
    *off = best_at;
    return 0;
}

int vram_reserve(mga_vram *v, uint32_t off, uint32_t bytes, uint32_t owner)
{
    int i;
    if (!bytes || !owner)
        return -1;
    for (i = 0; i < v->n; i++)
        if (off >= v->b[i].off && off - v->b[i].off < v->b[i].size)
            break;
    if (i == v->n || v->b[i].owner || bytes > v->b[i].off + v->b[i].size - off)
        return -1;
    return take(v, i, off, bytes, owner);
}

int vram_free(mga_vram *v, uint32_t off)
{
    int i;
    for (i = 0; i < v->n; i++)
        if (v->b[i].off == off && v->b[i].owner) {
            v->b[i].owner = 0;
            settle(v, i);
            return 0;
        }
    return -1;
}

void vram_free_owner(mga_vram *v, uint32_t owner)
{
    int i = 0;
    while (i < v->n) {
        if (v->b[i].owner == owner) {
            int left = i > 0 && !v->b[i - 1].owner;
            v->b[i].owner = 0;
            settle(v, i);
            if (left)
                i--;                            /* (taken into the free block before it) */
        }
        i++;
    }
}

uint32_t vram_size_of(const mga_vram *v, uint32_t off)
{
    int i;
    for (i = 0; i < v->n; i++)
        if (v->b[i].off == off && v->b[i].owner)
            return v->b[i].size;
    return 0;
}

uint32_t vram_free_bytes(const mga_vram *v, uint32_t *largest)
{
    uint32_t sum = 0, big = 0;
    int i;
    for (i = 0; i < v->n; i++)
        if (!v->b[i].owner) {
            sum += v->b[i].size;
            if (v->b[i].size > big)
                big = v->b[i].size;
        }
    if (largest)
        *largest = big;
    return sum;
}

uint32_t vram_owned_bytes(const mga_vram *v, uint32_t owner)
{
    uint32_t sum = 0;
    int i;
    for (i = 0; i < v->n; i++)
        if (v->b[i].owner == owner)
            sum += v->b[i].size;
    return sum;
}
