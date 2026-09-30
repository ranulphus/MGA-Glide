/* VECCHK save|check - check that a program left the machine's interrupt
 * set-up as it found it. "save" writes the real-mode vectors of IRQ 0, 1, 5,
 * 7 and 12 (INT 08h, 09h, 0Dh, 0Fh, 74h) and the PIC mask bits of those
 * IRQs to C:\OUT\VECCHK.DAT; "check" compares them with the machine now and
 * reports on COM1: "HX-VECCHK ok", or one "HX-VECCHK changed ..." line per
 * difference. Other IRQs' mask bits are not compared: the BIOS's own INT 15h
 * wait (DJGPP's delay()) leaves IRQ 8 unmasked, for one. */
#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>

#define FILE_NAME "C:\\OUT\\VECCHK.DAT"
static const unsigned char vecs[] = { 0x08, 0x09, 0x0d, 0x0f, 0x74 };
#define NV (sizeof vecs)
#define PIC1_BITS 0xA3                          /* IRQ 0, 1, 5, 7 */
#define PIC2_BITS 0x10                          /* IRQ 12 */

struct state {
    unsigned long vec[NV];
    unsigned char pic1, pic2;
};

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void puts_com(const char *s)
{
    while (*s) put(*s++);
}

static void read_state(struct state *s)
{
    unsigned long far *ivt = (unsigned long far *)MK_FP(0, 0);
    unsigned i;
    _disable();
    for (i = 0; i < NV; i++)
        s->vec[i] = ivt[vecs[i]];
    s->pic1 = (unsigned char)(inp(0x21) & PIC1_BITS);
    s->pic2 = (unsigned char)(inp(0xA1) & PIC2_BITS);
    _enable();
}

int main(int argc, char **argv)
{
    struct state now, was;
    char line[80];
    unsigned i, bad = 0;
    FILE *f;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    read_state(&now);
    if (argc > 1 && !strcmp(argv[1], "save")) {
        f = fopen(FILE_NAME, "wb");
        if (!f || fwrite(&now, sizeof now, 1, f) != 1) {
            puts_com("HX-VECCHK cannot write " FILE_NAME "\r\n");
            return 2;
        }
        fclose(f);
        puts_com("HX-VECCHK saved\r\n");
        return 0;
    }
    f = fopen(FILE_NAME, "rb");
    if (!f || fread(&was, sizeof was, 1, f) != 1) {
        puts_com("HX-VECCHK no saved state (run VECCHK save first)\r\n");
        return 2;
    }
    fclose(f);
    for (i = 0; i < NV; i++)
        if (now.vec[i] != was.vec[i]) {
            sprintf(line, "HX-VECCHK changed int=%02x was=%08lx now=%08lx\r\n", vecs[i], was.vec[i], now.vec[i]);
            puts_com(line);
            bad++;
        }
    if (now.pic1 != was.pic1 || now.pic2 != was.pic2) {
        sprintf(line, "HX-VECCHK changed pic was=%02x/%02x now=%02x/%02x\r\n", was.pic1, was.pic2, now.pic1, now.pic2);
        puts_com(line);
        bad++;
    }
    if (!bad)
        puts_com("HX-VECCHK ok\r\n");
    return bad ? 1 : 0;
}
