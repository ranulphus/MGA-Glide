; V86TEST: the switch from real mode into protected mode with paging and
; back (Open Watcom wasm, small model).
;
; unsigned long __cdecl pm_call(unsigned gdtr, unsigned long cr3,
;                               unsigned long entry, unsigned long arg);
;   gdtr   near pointer (DS) to a 6-byte GDT pseudo-descriptor
;   cr3    physical address of the page directory
;   entry  linear address of the 32-bit monitor's entry point
;   arg    passed to the monitor in ESI
; The monitor runs with CS=08h and DS=ES=SS=10h (flat, paging on) on a
; stack just below the caller's. It comes back with a far jump to
; SEL_CODE16:_pm_ret, with EAX = its result and interrupts disabled; that
; leaves protected mode and returns EAX to the caller in DX:AX.
.386p

_TEXT   segment word public 'CODE' use16
        assume  cs:_TEXT

        public  _pm_call
        public  _pm_ret

_pm_call proc near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        pushf
        cli
        mov     cs:save_ss, ss
        mov     cs:save_sp, sp
        mov     cs:save_ds, ds
        mov     ax, cs
        mov     cs:rm_cs, ax            ; target of the far jump back to real mode
        xor     eax, eax                ; the monitor's first stack: below ours
        mov     ax, ss
        shl     eax, 4
        movzx   ecx, sp
        add     eax, ecx
        sub     eax, 64
        mov     cs:pm_esp, eax
        mov     ebx, [bp+10]            ; entry
        mov     esi, [bp+14]            ; arg
        mov     eax, [bp+6]             ; cr3
        mov     cr3, eax
        mov     di, [bp+4]
        db      66h                     ; 32-bit GDT base
        lgdt    fword ptr [di]
        mov     eax, cr0
        or      eax, 80000001h          ; PG and PE: this code is identity-mapped
        mov     cr0, eax
        db      0EAh                    ; jmp SEL_CODE16:pm16
        dw      offset pm16
        dw      18h
pm16:
        mov     ax, 10h
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, cs:pm_esp
        mov     eax, 8
        push    eax                     ; 32-bit far return to 08h:entry
        push    ebx
        db      66h
        retf

_pm_ret label near
        mov     bx, 20h                 ; 16-bit data: real-mode limits in the caches
        mov     ds, bx
        mov     es, bx
        mov     fs, bx
        mov     gs, bx
        mov     ss, bx
        mov     ecx, cr0
        and     ecx, 7FFFFFFEh          ; paging and protection off
        mov     cr0, ecx
        db      0EAh                    ; jmp far rm_cs:rm_back
        dw      offset rm_back
rm_cs   dw      0
rm_back:
        mov     ss, cs:save_ss
        mov     sp, cs:save_sp
        mov     ds, cs:save_ds
        lidt    fword ptr cs:rm_idtr
        mov     edx, eax
        shr     edx, 16                 ; DX:AX = the monitor's result
        popf
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
_pm_call endp

save_ss dw      0
save_sp dw      0
save_ds dw      0
pm_esp  dd      0
rm_idtr dw      3FFh                    ; the real-mode interrupt vector table
        dd      0

_TEXT   ends
        end
