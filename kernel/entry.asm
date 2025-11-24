BITS 64

section .text
    global _start
    global context_switch
    global _iret_stub
    extern kmain
    extern gdt_init
    extern interrupts_init
    extern paging_init
    extern syslog_init
    extern __bss_start
    extern __bss_end
    extern g_kernel_stack_top

_start:
    cli
    mov rbp, 0
    and rsp, -16
    sub rsp, 8

    mov rsp, [g_kernel_stack_top]
    and rsp, -16
    sub rsp, 8

    mov r12, rdi ; BootInfo

    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor rax, rax
    rep stosb

    call syslog_init
    
    mov rdi, r12
    call paging_init
    
    call gdt_init
    call interrupts_init

    sti

    mov rdi, r12
    call kmain

.hang:
    hlt
    jmp .hang

; void context_switch(uint64_t* old_sp_ptr, uint64_t new_sp)
; RDI = old_sp_ptr
; RSI = new_sp
context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp      ; Save old RSP
    mov rsp, rsi        ; Load new RSP

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; Used by spawn_user_task to exit kernel mode
_iret_stub:
    iretq
