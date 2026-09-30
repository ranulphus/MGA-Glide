/* mousetst - a Loop A check of --mouse: 86Box's PS/2 mouse, CuteMouse
 * (loaded by RUN.BAT) and the monitor's "mouse DX DY BUTTONS" command
 * (local patch 0104) end to end. The harness moves the mouse right and up
 * with the left button held, then lets go (make loopa-selftest):
 *
 *   --keys '@HX-TEST driver,1:mouse:40:-20:1,2:mouse:40:-20:0'
 *
 * (timed from this program's driver line, so after its INT 33h reset)
 *
 * INT 33h function 0 finds the driver, 0Bh reads the motion counters
 * (mickeys since the last call) and 03h the buttons, for up to 30 seconds.
 * Built with DJGPP. */
#include "hx.h"
#include <dos.h>
#include <dpmi.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    __dpmi_regs r;
    long dx = 0, dy = 0;
    int buttons = 0, left_seen = 0, released = 0;
    uclock_t end;

    hx_init(argc, argv, "mousetst");
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0000;
    __dpmi_int(0x33, &r);
    hx_test("driver", r.x.ax == 0xffff, "INT 33h reset: ax=%04x buttons=%d", r.x.ax, r.x.bx);
    if (r.x.ax != 0xffff)
        hx_done(1);

    end = uclock() + 30 * UCLOCKS_PER_SEC;
    while (uclock() < end && !(left_seen && released && dx > 0 && dy < 0)) {
        memset(&r, 0, sizeof r);
        r.x.ax = 0x000b;
        __dpmi_int(0x33, &r);
        dx += (short)r.x.cx;
        dy += (short)r.x.dx;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0003;
        __dpmi_int(0x33, &r);
        buttons = r.x.bx;
        if (buttons & 1)
            left_seen = 1;
        else if (left_seen)
            released = 1;
        delay(20);
    }
    hx_stat("mouse dx=%ld dy=%ld buttons=%d left_seen=%d released=%d", dx, dy, buttons, left_seen, released);
    hx_test("motion", dx > 0 && dy < 0, "right and up: dx=%ld dy=%ld mickeys", dx, dy);
    hx_test("buttons", left_seen && released, "left pressed then released");
    hx_done(hx_failures() ? 1 : 0);
    return 0;
}
