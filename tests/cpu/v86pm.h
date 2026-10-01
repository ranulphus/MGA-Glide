/* V86TEST: what the 16-bit loader (v86test.c, Open Watcom) hands the 32-bit
 * monitor (pm_main.c, gcc -m32) in protected mode. Every field is 32 bits,
 * so both compilers lay it out the same way. */
#ifndef V86PM_H
#define V86PM_H

#ifdef __WATCOMC__
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

#define V86PM_MAGIC 0x50363856UL                /* "V86P" */
#define V86PM_BASE  0x00400000UL                /* where the monitor is linked and mapped */

/* Selectors the loader's GDT (and the monitor's copy of it) provide. */
#define SEL_CODE32  0x08
#define SEL_DATA32  0x10
#define SEL_CODE16  0x18                        /* base = loader CS * 16 */
#define SEL_DATA16  0x20                        /* base = loader DS * 16 */

struct bootinfo {
    u32 magic;
    u32 cs_base, ds_base;       /* the loader's real-mode CS and DS, times 16 */
    u32 ret_off;                /* offset of pm_ret in the loader's code segment */
    u32 pd, pt0, pt1;           /* physical: page directory, tables for 0-4 MB and 4-8 MB */
    u32 v86_seg;                /* 8 KB scratch area for V86 code, data and stack */
    u32 spare_a, spare_b;       /* two spare 4 KB pages (physical) */
    u32 blob_phys, blob_pages;  /* where the monitor's pages are */
    u32 fails;                  /* set by the monitor */
};

#endif
