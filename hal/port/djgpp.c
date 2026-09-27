/* djgpp.c - HAL port for DJGPP programs under CWSDPMI (DOS-GL).
 *
 * Apertures are mapped with DPMI 0800h and reached through near pointers
 * (__djgpp_nearptr_enable), so a mapped linear address is offset by
 * __djgpp_base_address. Callers that toggle nearptr off must re-enable it
 * before calling into the HAL. */
#include "mga/sys.h"
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/exceptn.h>
#include <string.h>
#include <sys/nearptr.h>
#include <time.h>

uint8_t  sys_inb(uint16_t p) { return inportb(p); }
uint16_t sys_inw(uint16_t p) { return inportw(p); }
uint32_t sys_inl(uint16_t p) { return inportl(p); }
void sys_outb(uint16_t p, uint8_t v) { outportb(p, v); }
void sys_outw(uint16_t p, uint16_t v) { outportw(p, v); }
void sys_outl(uint16_t p, uint32_t v) { outportl(p, v); }

volatile void *sys_map_phys(uint32_t phys, uint32_t size)
{
    __dpmi_meminfo mi;
    if (!__djgpp_nearptr_enable())
        return NULL;
    mi.address = phys;
    mi.size = size;
    if (__dpmi_physical_address_mapping(&mi) != 0)
        return NULL;
    return (volatile void *)(mi.address - (uint32_t)__djgpp_base_address);
}

void sys_unmap_phys(volatile void *p, uint32_t size)
{
    __dpmi_meminfo mi;
    mi.address = (uint32_t)(uintptr_t)p + (uint32_t)__djgpp_base_address;
    mi.size = size;
    __dpmi_free_physical_address_mapping(&mi);
}

int sys_rm_int(uint8_t intno, sys_rmregs *r)
{
    __dpmi_regs d;
    memset(&d, 0, sizeof d);
    d.d.edi = r->edi; d.d.esi = r->esi; d.d.ebp = r->ebp;
    d.d.ebx = r->ebx; d.d.edx = r->edx; d.d.ecx = r->ecx; d.d.eax = r->eax;
    d.x.es = r->es; d.x.ds = r->ds; d.x.fs = r->fs; d.x.gs = r->gs;
    if (__dpmi_int(intno, &d) != 0)
        return -1;
    r->edi = d.d.edi; r->esi = d.d.esi; r->ebp = d.d.ebp;
    r->ebx = d.d.ebx; r->edx = d.d.edx; r->ecx = d.d.ecx; r->eax = d.d.eax;
    r->flags = d.x.flags; r->es = d.x.es; r->ds = d.x.ds;
    return 0;
}

#define DOS_BLOCKS 8
static struct { void *p; int sel; } dos_blocks[DOS_BLOCKS];

void *sys_dos_alloc(uint32_t bytes, uint16_t *rm_segment)
{
    int i, sel, seg;
    for (i = 0; i < DOS_BLOCKS && dos_blocks[i].p; i++)
        ;
    if (i == DOS_BLOCKS || !__djgpp_nearptr_enable())
        return NULL;
    seg = __dpmi_allocate_dos_memory((int)((bytes + 15) >> 4), &sel);
    if (seg < 0)
        return NULL;
    *rm_segment = (uint16_t)seg;
    dos_blocks[i].p = (void *)((uint32_t)seg * 16u - (uint32_t)__djgpp_base_address);
    dos_blocks[i].sel = sel;
    return dos_blocks[i].p;
}

void sys_dos_free(void *p)
{
    int i;
    for (i = 0; i < DOS_BLOCKS; i++) {
        if (dos_blocks[i].p == p) {
            __dpmi_free_dos_memory(dos_blocks[i].sel);
            dos_blocks[i].p = NULL;
        }
    }
}

uint32_t sys_time_us(void)
{
    return (uint32_t)((unsigned long long)uclock() * 1000000ull / UCLOCKS_PER_SEC);
}

void sys_delay_us(uint32_t us)
{
    uint32_t start = sys_time_us();
    while ((uint32_t)(sys_time_us() - start) < us)
        ;
}

void *sys_alloc(uint32_t bytes) { return malloc(bytes); }
void sys_free(void *p) { free(p); }

void *sys_real_ptr(uint16_t seg, uint16_t off)
{
    __djgpp_nearptr_enable();
    return (void *)((((uint32_t)seg << 4) + off) - (uint32_t)__djgpp_base_address);
}

/* ---- Fault and exit hooks ----------------------------------------------
 * DJGPP turns CPU exceptions into signals: #GP and #PF into SIGSEGV, #UD
 * into SIGILL, divide error into SIGFPE; the exception number and faulting
 * EIP are in __djgpp_exception_state. The hook runs first, then the
 * previous handler (DJGPP's default prints the traceback and exits). The
 * exit hook runs from atexit and on Ctrl-Break (SIGINT). */
static const int fault_sigs[] = { SIGSEGV, SIGILL, SIGFPE };
static void (*prev_fault[3])(int);
static sys_fault_fn fault_fn;
static sys_exit_fn exit_fn;
static void (*prev_int)(int);
static int exit_registered, in_hook;

static void chain(int sig, void (*prev)(int))
{
    signal(sig, prev == SIG_IGN ? SIG_DFL : prev);
    if (prev != SIG_DFL && prev != SIG_IGN && prev != SIG_ERR)
        prev(sig);
    else
        raise(sig);
}

static void on_fault(int sig)
{
    int i;
    for (i = 0; i < 3 && fault_sigs[i] != sig; i++)
        ;
    if (fault_fn && !in_hook) {
        in_hook = 1;
        fault_fn(__djgpp_exception_state_ptr ? (int)__djgpp_exception_state->__signum : -1, 0,
                 __djgpp_exception_state_ptr ? (uint32_t)__djgpp_exception_state->__eip : 0);
        in_hook = 0;
    }
    chain(sig, i < 3 ? prev_fault[i] : SIG_DFL);
}

int sys_hook_faults(sys_fault_fn fn)
{
    int i;
    if (!fault_fn)
        for (i = 0; i < 3; i++)
            prev_fault[i] = signal(fault_sigs[i], on_fault);
    fault_fn = fn;
    return 0;
}

void sys_unhook_faults(void)
{
    int i;
    if (!fault_fn)
        return;
    for (i = 0; i < 3; i++)
        signal(fault_sigs[i], prev_fault[i]);
    fault_fn = NULL;
}

static void run_exit_hook(void)
{
    sys_exit_fn fn = exit_fn;
    exit_fn = NULL;             /* once only */
    if (fn)
        fn();
}

static void on_break(int sig)
{
    run_exit_hook();
    chain(sig, prev_int);
}

int sys_hook_exit(sys_exit_fn fn)
{
    if (!exit_registered) {
        atexit(run_exit_hook);
        prev_int = signal(SIGINT, on_break);
        exit_registered = 1;
    }
    exit_fn = fn;
    return 0;
}

void sys_unhook_exit(void)
{
    exit_fn = NULL;
}

void sys_terminate(int code) { exit(code); }
