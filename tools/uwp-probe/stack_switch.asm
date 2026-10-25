; probe_call_on_stack(fn, arg, stack_top): call fn(arg) with RSP moved to stack_top.
; rcx = fn, rdx = arg, r8 = stack_top (16-byte aligned by this routine).
; Does not touch the TEB's stack bounds; the probe only checks where RSP lands.
.code

probe_call_on_stack PROC
    push rbp
    mov  rbp, rsp
    mov  rax, rcx
    mov  rcx, rdx
    mov  rsp, r8
    and  rsp, -16
    sub  rsp, 32
    call rax
    mov  rsp, rbp
    pop  rbp
    ret
probe_call_on_stack ENDP

END
