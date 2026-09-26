/* texfmt.c against the retail runtime's answers (tests/unit/data/texmem.txt,
 * recorded by TEXPROBE on GTA's and Screamer Rally's OVLs). */
#include "unit.h"
#include "tex/texfmt.h"
#include <stdio.h>

int unit_main(void)
{
    FILE *f = fopen("tests/unit/data/texmem.txt", "r");
    char kind[16];
    int n = 0, bad = 0;
    CHECK(f != NULL);
    if (!f)
        return 0;
    while (fscanf(f, "%15s", kind) == 1) {
        if (!strcmp(kind, "calc")) {
            int fmt, a, lg, sm; unsigned long v;
            if (fscanf(f, "%d %d %d %d %lu", &fmt, &a, &lg, &sm, &v) != 5) break;
            if (tex_mem_required(sm, lg, a, fmt, 3) != v && bad++ < 5)
                fprintf(stderr, "calc %d %d %d %d: want %lu got %u\n", fmt, a, lg, sm, v, tex_mem_required(sm, lg, a, fmt, 3));
            n++;
        } else if (!strcmp(kind, "req")) {
            int eo, fmt, a, lg, sm; unsigned long v;
            if (fscanf(f, "%d %d %d %d %d %lu", &eo, &fmt, &a, &lg, &sm, &v) != 6) break;
            if (tex_mem_required(sm, lg, a, fmt, (FxU32)eo) != v && bad++ < 5)
                fprintf(stderr, "req %d %d %d %d %d: want %lu got %u\n", eo, fmt, a, lg, sm, v,
                        tex_mem_required(sm, lg, a, fmt, (FxU32)eo));
            n++;
        } else {
            int c;
            while ((c = fgetc(f)) != '\n' && c != EOF) ;
        }
    }
    fclose(f);
    printf("texfmt: %d retail data points checked\n", n);
    CHECK(n > 16000);
    CHECK_EQ(bad, 0);
    return 0;
}
