/* REBOOT - reset the machine through the keyboard controller. */
#include <conio.h>

int main(void)
{
    long spin;
    for (spin = 0; spin < 100000L && (inp(0x64) & 0x02); spin++)
        continue;
    outp(0x64, 0xFE);
    for (;;)
        continue;
}
