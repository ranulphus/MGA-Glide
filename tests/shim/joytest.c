/* joytest - a Loop A check of the joystick path: 86Box's game port, the
 * virtual joystick (local patch 0105) and the monitor's "joy" command, end
 * to end. The harness (make loopa-selftest) moves each axis of a 4-axis
 * stick to one end and then the other, then presses each button, timed from
 * this program's centre line:
 *
 *   --keys '@HX-TEST centre,1:joy:axis:0:-32767,2:joy:axis:0:32767,...'
 *
 * Port 0x201: a write starts the four one-shots; bits 0-3 stay set for a
 * time that grows with each axis' resistance, bits 4-7 are the buttons
 * (0 when pressed). Axis times are measured with uclock() (0.84 us ticks).
 * Each step waits up to 10 seconds. Built with DJGPP. */
#include "hx.h"
#include <dos.h>
#include <pc.h>
#include <stdio.h>
#include <time.h>

#define AXES 4
#define TIMEOUT_TICKS (UCLOCKS_PER_SEC / 100)   /* 10 ms: longer than any stick */

/* One read: the one-shot time of each axis in uclock ticks (-1 if it never
 * fell within the timeout), and the button bits (1 = pressed). */
static int read_port(long t[AXES])
{
    uclock_t start, now;
    int pending = (1 << AXES) - 1, b, i;

    b = (~inportb(0x201) >> 4) & 15;
    for (i = 0; i < AXES; i++)
        t[i] = -1;
    disable();
    outportb(0x201, 0xff);
    start = uclock();
    do {
        int v = inportb(0x201);
        now = uclock();
        for (i = 0; i < AXES; i++)
            if ((pending & (1 << i)) && !(v & (1 << i))) {
                t[i] = (long)(now - start);
                pending &= ~(1 << i);
            }
    } while (pending && now - start < TIMEOUT_TICKS);
    enable();
    return b;
}

/* Wait until cond(axis reading) holds, up to 10 s; returns the last reading. */
static long wait_axis(int axis, long centre, int want_low)
{
    uclock_t end = uclock() + 10 * UCLOCKS_PER_SEC;
    long t[AXES];
    do {
        read_port(t);
        if (t[axis] >= 0 && (want_low ? t[axis] < centre / 2 : t[axis] > centre * 3 / 2))
            return t[axis];
        delay(10);
    } while (uclock() < end);
    return t[axis];
}

int main(int argc, char **argv)
{
    long t[AXES], centre[AXES], lo, hi;
    int i, b;
    char name[32];

    hx_init(argc, argv, "joytest");
    b = read_port(t);
    for (i = 0; i < AXES; i++)
        centre[i] = t[i];
    hx_test("centre", t[0] > 0 && t[1] > 0 && t[2] > 0 && t[3] > 0 && b == 0,
            "one-shots %ld %ld %ld %ld ticks, buttons %x", t[0], t[1], t[2], t[3], b);
    if (hx_failures())
        hx_done(1);

    for (i = 0; i < AXES; i++) {
        lo = wait_axis(i, centre[i], 1);
        hi = wait_axis(i, centre[i], 0);
        sprintf(name, "axis%d", i);
        hx_test(name, lo >= 0 && lo < centre[i] / 2 && hi > centre[i] * 3 / 2,
                "centre %ld, low end %ld, high end %ld ticks", centre[i], lo, hi);
    }
    for (i = 0; i < 4; i++) {
        uclock_t end = uclock() + 10 * UCLOCKS_PER_SEC;
        do {
            b = read_port(t);
            if (b & (1 << i))
                break;
            delay(10);
        } while (uclock() < end);
        sprintf(name, "button%d", i);
        hx_test(name, (b & (1 << i)) != 0, "buttons read %x", b);
    }
    hx_done(hx_failures() ? 1 : 0);
    return 0;
}
