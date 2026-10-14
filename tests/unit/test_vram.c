/* test_vram.c - the VRAM allocator (hal/src/vram.c): set cases, then a
 * long random run checked against a map of who owns each 64-byte unit. */
#include "unit.h"
#include "mga/hal.h"
#include <stdlib.h>

static mga_vram v;

/* The table is in order, covers [base, end) without gaps, has no empty
   blocks and no two free blocks side by side. */
static int sound(void)
{
    uint32_t at = v.base;
    int i;
    if (v.n < 1 || v.n > MGA_VRAM_BLOCKS)
        return 0;
    for (i = 0; i < v.n; i++) {
        if (v.b[i].off != at || !v.b[i].size)
            return 0;
        if (i && !v.b[i].owner && !v.b[i - 1].owner)
            return 0;
        at += v.b[i].size;
    }
    return at == v.end;
}

static void cases(void)
{
    uint32_t a, b, c, d, big;

    vram_init(&v, 0, 8u << 20);
    CHECK(sound());
    CHECK_EQ(vram_free_bytes(&v, &big), 8u << 20);
    CHECK_EQ(big, 8u << 20);

    /* The front buffer where the display starts; the cursor at the top, 1 KB aligned. */
    CHECK_EQ(vram_reserve(&v, 0, 1024 * 768 * 2, 1), 0);
    CHECK_EQ(vram_alloc(&v, 1024, 1024, MGA_VRAM_TOP, 2, &a), 0);
    CHECK_EQ(a, (8u << 20) - 1024);
    CHECK_EQ(vram_alloc(&v, 100, 64, 0, 3, &b), 0);
    CHECK_EQ(b, 1024 * 768 * 2);
    CHECK_EQ(vram_alloc(&v, 100, 64, 0, 3, &c), 0);
    CHECK_EQ(c, 1024 * 768 * 2 + 128);              /* (the first ended at +100: aligned up) */
    CHECK_EQ(vram_size_of(&v, c), 100);
    CHECK(sound());

    /* No room, overlapping reserves, bad arguments. */
    CHECK_EQ(vram_alloc(&v, 8u << 20, 64, 0, 4, &d), -1);
    CHECK_EQ(vram_reserve(&v, 4096, 64, 4), -1);
    CHECK_EQ(vram_alloc(&v, 64, 48, 0, 4, &d), -1);
    CHECK_EQ(vram_alloc(&v, 64, 64, 0, 0, &d), -1);
    CHECK_EQ(vram_alloc(&v, 0, 64, 0, 4, &d), -1);
    CHECK_EQ(vram_free(&v, c + 4), -1);

    /* Freeing merges with free neighbours. */
    CHECK_EQ(vram_free(&v, b), 0);
    CHECK(sound());
    CHECK_EQ(vram_free(&v, b), -1);
    CHECK_EQ(vram_free(&v, c), 0);
    CHECK(sound());
    CHECK_EQ(v.n, 3);                               /* front buffer, free, cursor */
    CHECK_EQ(vram_owned_bytes(&v, 1), 1024 * 768 * 2);
    CHECK_EQ(vram_free_bytes(&v, &big), (8u << 20) - 1024 * 768 * 2 - 1024);
    CHECK_EQ(big, (8u << 20) - 1024 * 768 * 2 - 1024);

    /* An owner's blocks go together, wherever they are. */
    CHECK_EQ(vram_alloc(&v, 4096, 64, 0, 5, &a), 0);
    CHECK_EQ(vram_alloc(&v, 4096, 64, 0, 6, &b), 0);
    CHECK_EQ(vram_alloc(&v, 4096, 64, 0, 5, &c), 0);
    CHECK_EQ(vram_alloc(&v, 4096, 64, MGA_VRAM_TOP, 5, &d), 0);
    vram_free_owner(&v, 5);
    CHECK(sound());
    CHECK_EQ(vram_owned_bytes(&v, 5), 0);
    CHECK_EQ(vram_size_of(&v, b), 4096);
    CHECK_EQ(vram_free(&v, b), 0);
    CHECK_EQ(v.n, 3);

    /* A reserve inside a free block splits it in three. */
    vram_init(&v, 0x1000, 0x9000);
    CHECK_EQ(vram_reserve(&v, 0x4000, 0x1000, 7), 0);
    CHECK_EQ(v.n, 3);
    CHECK_EQ(vram_reserve(&v, 0x8000, 0x2000, 7), -1);     /* past the end */
    CHECK_EQ(vram_reserve(&v, 0x0, 0x10, 7), -1);          /* before the start */
    CHECK(sound());

    /* The table full: allocations fail cleanly and the table stays sound. */
    vram_init(&v, 0, 1u << 24);
    {
        int i, fails = 0;
        for (i = 0; i < 2 * MGA_VRAM_BLOCKS; i++)
            if (vram_alloc(&v, 64, 64, 0, 1 + (uint32_t)(i & 1), &a) != 0)
                fails++;
        CHECK(fails > 0);
        CHECK(sound());
        vram_free_owner(&v, 2);                     /* every other block: free ones between owned ones */
        CHECK(sound());
        CHECK_EQ(vram_reserve(&v, 64, 64, 3), 0);   /* (into a free block exactly its size: no new blocks) */
        CHECK(sound());
    }
}

#define UNITS 4096                                  /* of 64 bytes: 256 KB */
static uint8_t who[UNITS];

static void random_run(void)
{
    struct { uint32_t off, bytes; uint8_t owner; } live[400];
    int nlive = 0, step, bad = 0;
    unsigned seed = 12345;
    vram_init(&v, 0, UNITS * 64);
    memset(who, 0, sizeof who);
    for (step = 0; step < 200000 && !bad; step++) {
        seed = seed * 1103515245u + 12345u;
        int r = (int)((seed >> 16) % 100);
        if (r < 55 && nlive < (int)MGA_ARRAY_LEN(live)) {
            uint32_t bytes = 1 + (seed >> 8) % 6000, off, align = 64u << ((seed >> 4) % 5), u;
            uint8_t owner = (uint8_t)(1 + (seed >> 24) % 7);
            unsigned flags = (seed >> 3) & 1 ? MGA_VRAM_TOP : 0;
            if (vram_alloc(&v, bytes, align, flags, owner, &off) == 0) {
                if (off & (align - 1))
                    bad = 1;
                for (u = off / 64; u < (off + bytes + 63) / 64; u++) {
                    if (who[u])
                        bad = 2;
                    who[u] = owner;
                }
                live[nlive].off = off, live[nlive].bytes = bytes, live[nlive].owner = owner;
                nlive++;
            }
        } else if (r < 95 && nlive) {
            int k = (int)((seed >> 8) % (unsigned)nlive);
            uint32_t u;
            if (vram_free(&v, live[k].off) != 0)
                bad = 3;
            for (u = live[k].off / 64; u < (live[k].off + live[k].bytes + 63) / 64; u++)
                who[u] = 0;
            live[k] = live[--nlive];
        } else if (nlive) {
            uint8_t owner = (uint8_t)(1 + (seed >> 24) % 7);
            int k;
            uint32_t u;
            vram_free_owner(&v, owner);
            for (k = 0; k < nlive; k++)
                if (live[k].owner == owner) {
                    for (u = live[k].off / 64; u < (live[k].off + live[k].bytes + 63) / 64; u++)
                        who[u] = 0;
                    live[k--] = live[--nlive];
                }
        }
        if (!sound())
            bad = 4;
        if (!(step & 255)) {                        /* the table agrees with the map, unit by unit */
            uint32_t u, owned = 0;
            int i;
            for (i = 0; i < nlive; i++)
                owned += live[i].bytes;
            for (i = 0; i < v.n; i++)
                for (u = (v.b[i].off + 63) / 64; u < (v.b[i].off + v.b[i].size) / 64; u++)    /* (units wholly inside) */
                    if (!v.b[i].owner && who[u])
                        bad = 5;
            if (vram_free_bytes(&v, NULL) != UNITS * 64 - owned)
                bad = 6;
        }
    }
    if (bad)
        fprintf(stderr, "random run: failure %d at step %d\n", bad, step);
    CHECK_EQ(bad, 0);
}

int unit_main(void)
{
    cases();
    random_run();
    return 0;
}
