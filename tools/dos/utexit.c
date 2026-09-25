/* UTEXIT <code> - end the 86Box run with an exit code via the unit tester
 * (no effect on real hardware). Used by RUN.BAT after games exit. */
#include <conio.h>
#include <dos.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    static const char magic[] = "86Box";
    int i, code = argc > 1 ? atoi(argv[1]) : 0;
    long spin;
    _disable();
    for (i = 0; i < 5; i++) outp(0x80, magic[i]);
    outp(0x80, 0x80); outp(0x80, 0x0E);
    _enable();
    if (inp(0xE80) == 0xFF) return code;
    outp(0xE80, 0x04);
    for (spin = 0; spin < 1000000L && !(inp(0xE80) & 0x02); spin++) ;
    outp(0xE81, code & 0x7F);
    return code;
}
