/* Formatter used by the runtime's logging. */
#include "unit.h"
#include "rt/rt.h"

int unit_main(void)
{
    char b[64];
    mg_snprintf(b, sizeof b, "%d %u %x %X %s %c %%", -42, 42u, 0xbeefu, 0xbeefu, "hi", 'z');
    CHECK_STR(b, "-42 42 beef BEEF hi z %");
    mg_snprintf(b, sizeof b, "[%5d][%-5d][%05d][%08x]", 12, 12, -12, 0x1234u);
    CHECK_STR(b, "[   12][12   ][-0012][00001234]");
    mg_snprintf(b, sizeof b, "%.2f %f", 3.14159, -0.5);
    CHECK_STR(b, "3.14 -0.500");
    CHECK_EQ(mg_snprintf(b, 4, "abcdef"), 6);
    CHECK_STR(b, "abc");
    mg_snprintf(b, sizeof b, "%s", (const char *)0);
    CHECK_STR(b, "(null)");
    return 0;
}
