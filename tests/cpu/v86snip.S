/* V86TEST code under test. The 16-bit snippets are copied to offset 0 of the
 * V86 scratch segment and run in virtual-8086 mode (CS=DS=ES=SS); each ends
 * in HLT, which traps (#GP) back to the monitor. The 32-bit ring-3 code runs
 * from the monitor's user pages (.user). */

        .macro  snip name
        .globl  \name, \name\()_end
        .endm

        .section .rodata.v86, "a"
        .code16

        snip    v86_b
v86_b:  movw    $0x1234, %ax
        hlt
v86_b_end:

        snip    v86_hlt
v86_hlt: hlt
v86_hlt_end:

/* IOPL-sensitive instructions, each after a NOP (so it sits at IP 1). */
        snip    v86_cli
v86_cli: nop
        cli
        hlt
v86_cli_end:
        snip    v86_sti
v86_sti: nop
        sti
        hlt
v86_sti_end:
        snip    v86_pushf
v86_pushf: nop
        pushfw
        hlt
v86_pushf_end:
        snip    v86_popf
v86_popf: nop
        popfw
        hlt
v86_popf_end:
        snip    v86_pushfd
v86_pushfd: nop
        pushfl
        hlt
v86_pushfd_end:
        snip    v86_popfd
v86_popfd: nop
        popfl
        hlt
v86_popfd_end:
        snip    v86_int21
v86_int21: nop
        int     $0x21
        hlt
v86_int21_end:
        snip    v86_iret
v86_iret: nop
        iretw
        hlt
v86_iret_end:
        snip    v86_iretd
v86_iretd: nop
        iretl
        hlt
v86_iretd_end:

/* INT3, INTO (OF set by the caller) and BOUND out of range. */
        snip    v86_int3
v86_int3: nop
        int3
        hlt
v86_int3_end:
        snip    v86_into
v86_into: nop
        into
        hlt
v86_into_end:
        snip    v86_bound
v86_bound: movw $5, %ax
        boundw  %ax, v86_bound_lim - v86_bound
        hlt
v86_bound_lim: .word 10, 20
v86_bound_end:

/* Port reads, port number in BX; the I/O instruction is at IP 3. */
        snip    v86_in8
v86_in8: movw   %bx, %dx
        nop
        inb     %dx, %al
        hlt
v86_in8_end:
        snip    v86_in16
v86_in16: movw  %bx, %dx
        nop
        inw     %dx, %ax
        hlt
v86_in16_end:
        snip    v86_in32
v86_in32: movw  %bx, %dx
        nop
        inl     %dx, %eax
        hlt
v86_in32_end:
        snip    v86_insb
v86_insb: movw  %bx, %dx
        movw    $0x1800, %di
        insb
        hlt
v86_insb_end:

/* VME: CLI/STI/PUSHF/POPF against VIF. */
        snip    v86_cli_pushf
v86_cli_pushf: cli
        pushfw
        popw    %ax
        hlt
v86_cli_pushf_end:
        snip    v86_sti_pushf
v86_sti_pushf: sti
        pushfw
        popw    %ax
        hlt
v86_sti_pushf_end:
        snip    v86_popf_tf
v86_popf_tf: pushw $0x0100
        popfw
        hlt
v86_popf_tf_end:
        snip    v86_popf_if
v86_popf_if: pushw $0x0200
        popfw
        pushfw
        popw    %ax
        hlt
v86_popf_if_end:

/* VME: INT 60h redirected through the IVT to the handler below, which
   reads the FLAGS image the CPU pushed; INT 61h sent to protected mode. */
        snip    v86_int60
v86_int60: cli
        int     $0x60
        hlt
        .globl  v86_int60_h
v86_int60_h: movw %sp, %bp
        movw    4(%bp), %ax
        hlt
v86_int60_end:
        snip    v86_int61
v86_int61: nop
        int     $0x61
        hlt
v86_int61_end:

        snip    v86_pushf_pop
v86_pushf_pop: pushfw
        popw    %ax
        hlt
v86_pushf_pop_end:

/* Page faults on the scratch segment's second page (offset 1000h). */
        snip    v86_rd1000
v86_rd1000: movb 0x1000, %al
        hlt
v86_rd1000_end:
        snip    v86_wr1000
v86_wr1000: movb $1, 0x1000
        hlt
v86_wr1000_end:

        .code32

/* Ring-3 code and stack, on pages the monitor makes user-accessible. */
        .section .user, "awx"
        .align  4096
        .globl  user_stack, user_stack_top
user_stack:
        .space  4096
user_stack_top:

        .globl  r3_hlt, r3_in8, r3_cli, r3_sti, r3_popf, r3_int41, r3_int42, r3_esp, r3_rd, r3_wr
r3_hlt: hlt
r3_in8: movl    %ebx, %edx
        inb     %dx, %al
        hlt
r3_cli: cli
        pushfl
        popl    %eax
        hlt
r3_sti: sti
        pushfl
        popl    %eax
        hlt
r3_popf: pushl  %ebx
        popfl
        pushfl
        popl    %eax
        hlt
r3_int41: int   $0x41
        hlt
r3_int42: int   $0x42
        hlt
r3_esp: movl    %esp, %eax
        hlt
r3_rd:  movb    (%ebx), %al
        hlt
r3_wr:  movb    %al, (%ebx)
        hlt

        .section .note.GNU-stack, "", @progbits
