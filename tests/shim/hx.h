/* hx.h - guest-side test shim shared by every DOS test program.
 *
 * Reports over COM1 with a line protocol the harness parses:
 *   HX-START <prog> <build> ovl=<path>
 *   HX-TEST <name> PASS|FAIL [detail]
 *   HX-IMG <file> <w>x<h> crc=<crc32>
 *   HX-STAT k=v ...
 *   HX-DONE <code>
 * In 86Box the unit-tester device captures the screen and ends the run
 * with the test's exit code; on real hardware those calls are no-ops. */
#ifndef HX_H
#define HX_H
#include <stdint.h>

enum { HX_PASS = 0, HX_FAIL = 1, HX_INIT_FAILED = 2, HX_EXCEPTION = 3, HX_BAD_ARGS = 4 };

typedef struct {
    int frames;              /* --frames N (default 60) */
    int capture;             /* --capture K: capture after frame K (-1 = none) */
    int bench;               /* --bench */
    int fail, hang, crash;   /* harness self-tests */
    const char *glide;       /* --glide=PATH: OVL to load with leload */
    const char *out;         /* --out DIR (default C:\OUT) */
    int noexit;              /* --noexit: don't end the emulator at the end */
} hx_args_t;

extern hx_args_t hx_args;

void hx_init(int argc, char **argv, const char *prog);
void hx_log(const char *fmt, ...);              /* free-form line, also on stdout */
void hx_test(const char *name, int pass, const char *fmt, ...);
void hx_stat(const char *fmt, ...);
void hx_done(int code);                         /* never returns */
int  hx_failures(void);

/* 86Box unit tester (no-ops when absent). */
int      hx_ut_present(void);
int      hx_ut_capture(uint16_t *w, uint16_t *h);
uint32_t hx_ut_crc(int x, int y, int w, int h);
int      hx_ut_read(int x, int y, int w, int h, uint8_t *bgrx);
void     hx_ut_exit(int code);

/* Images: RGB888 rows, written to <out>\<name>.PPM and reported. */
uint32_t hx_crc32(uint32_t crc, const void *p, uint32_t n);
int      hx_save_ppm(const char *name, int w, int h, const uint8_t *rgb);
/* Capture the emulated display (unit tester) and save it. */
int      hx_snap_screen(const char *name);

#endif
