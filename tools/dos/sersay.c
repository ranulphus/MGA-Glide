/* SERSAY <text...> - write one line to COM1 (115200 8N1) for the harness. */
#include <conio.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

int main(int argc, char **argv)
{
    int i;
    char *p;
    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    for (i = 1; i < argc; i++) {
        if (i > 1) put(' ');
        for (p = argv[i]; *p; p++) put(*p);
    }
    put('\r'); put('\n');
    return 0;
}
