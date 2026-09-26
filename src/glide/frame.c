/* frame.c - per-frame bookkeeping: statistics lines, snapshot markers for
 * the harness, and the test exit (MGAGLIDE exit_after=N). */
#include "glide/mg.h"
#include "mga/sys.h"

mg_stats_t mg_stats;

static void ut_exit(int code)
{
    static const char magic[] = "86Box";
    int i;
    for (i = 0; i < 5; i++)
        sys_outb(0x80, (uint8_t)magic[i]);
    sys_outb(0x80, 0x80);
    sys_outb(0x80, 0x0E);
    if (sys_inb(0x0E80) == 0xFF)
        return;
    sys_outb(0x0E80, 0x04);
    for (i = 0; i < 100000 && !(sys_inb(0x0E80) & 0x02); i++)
        ;
    sys_outb(0x0E81, (uint8_t)(code & 0x7F));
}

void mg_frame_end(void)
{
    int i;
    for (i = 0; i < mg_config.nsnap; i++)
        if ((int)mg.frame == mg_config.snap[i])
            mg_line("MGL-SNAP %u", mg.frame);
    if (mg_config.stats_every && mg.frame % (unsigned)mg_config.stats_every == 0) {
        mg_line("MGL-STAT frame=%u tris=%u", mg.frame, mg_stats.tris);
        mg_stats.tris = 0;
    }
    if (mg_config.exit_after && (int)mg.frame >= mg_config.exit_after) {
        mg_line("MGL-EXIT frames=%u", mg.frame);
        vbe_set_text_mode();
        ut_exit(0);
    }
}
