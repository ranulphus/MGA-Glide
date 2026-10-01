/* V86TEST monitor: one flat image at V86PM_BASE (4 MB); the loader maps the
   file and then zeroed pages up to __bss_end. */
ENTRY(_start)
SECTIONS
{
    . = 0x400000;
    .text : { *(.text.head) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(4096);
    __user_start = .;
    .user : { *(.user) . = ALIGN(4096); }   /* padded, so the file covers it */
    . = ALIGN(4096);
    __user_end = .;
    __file_end = .;
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    . = ALIGN(4096);
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
