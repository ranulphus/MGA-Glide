/* WAITSEC <n> - wait n seconds using the BIOS tick counter. */
#include <dos.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    unsigned long far *ticks = (unsigned long far *)MK_FP(0x40, 0x6C);
    unsigned long start = *ticks, n = (unsigned long)(argc > 1 ? atoi(argv[1]) : 1) * 182UL / 10UL;
    while (*ticks - start < n)
        continue;
    return 0;
}
