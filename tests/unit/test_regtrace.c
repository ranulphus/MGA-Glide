/* test_regtrace.c - the register write ring keeps the newest writes in order. */
#include "unit.h"
#include "mga/mmio.h"
#include <stdio.h>
#include <string.h>

static char lines[300][96];
static int nlines;
static void out(const char *l) { if (nlines < 300) strcpy(lines[nlines++], l); }

int unit_main(void)
{
    uint32_t i;
    unsigned seq, off, v;
    mga_trace_clear();
    for (i = 0; i < 300; i++)
        mga_trace_record_at(0x1C00 + (i & 0xFF) * 4, i * 3, "x.c", (int)i);
    CHECK_EQ(mga_trace_count(), 300);
    nlines = 0;
    mga_trace_dump(out, 0);
    CHECK_EQ(nlines, 256);                          /* ring length */
    CHECK(sscanf(lines[0], "MGA-WR #%u %x=%x", &seq, &off, &v) == 3);
    CHECK_EQ(seq, 45);                              /* oldest kept: 300 - 256 + 1 */
    CHECK_EQ(v, 44 * 3);
    CHECK(sscanf(lines[255], "MGA-WR #%u %x=%x", &seq, &off, &v) == 3);
    CHECK_EQ(seq, 300);
    CHECK(strstr(lines[255], "x.c:299") != NULL);
    nlines = 0;
    mga_trace_dump(out, 3);
    CHECK_EQ(nlines, 3);
    CHECK(strstr(lines[2], "#300 ") != NULL);
    return 0;
}
