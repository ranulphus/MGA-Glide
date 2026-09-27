/* REBOOT - reset the machine for the next bench job.
 *
 * First a hard reset through the PCI reset control register (port CF9h:
 * 02h then 06h), which resets the chipset and devices too; a keyboard
 * controller reset (FEh to port 64h) only resets the CPU, and under 86Box's
 * i440BX machine that sometimes left the machine executing garbage at the
 * reset vector. The keyboard controller remains as the fallback for boards
 * without CF9h. The CMOS shutdown code is cleared first so the BIOS does a
 * normal POST rather than resuming through 40:67. */
#include <conio.h>

static void wait_ms(unsigned ms)
{
    /* Port 80h writes take about 1 us each on ISA timing. */
    long n = (long)ms * 1000L;
    while (n-- > 0)
        outp(0x80, 0);
}

int main(void)
{
    long spin;
    outp(0x70, 0x0F);                /* CMOS shutdown status: normal reset */
    outp(0x71, 0x00);
    outp(0xCF9, 0x02);
    outp(0xCF9, 0x06);
    wait_ms(500);
    for (spin = 0; spin < 100000L && (inp(0x64) & 0x02); spin++)
        continue;
    outp(0x64, 0xFE);
    for (;;)
        continue;
}
