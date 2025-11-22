BITS 64

section .text
    global _start
    extern kmain
    extern gdt_init
    extern interrupts_init
    extern paging_init
    extern syslog_init
    extern __bss_start
    extern __bss_end
    extern g_kernel_stack_top

_start:
    ; Disable interrupts while the IDT is being built
    cli

    mov rbp, 0
    and rsp, -16              ; Align the stack to 16 bytes for System V ABI
    sub rsp, 8                ; Account for a return address push

    ; Switch to the dedicated kernel stack
    mov rsp, [g_kernel_stack_top]
    and rsp, -16              ; Align
    sub rsp, 8                ; Account for return addr

    mov r12, rdi                ; preserve BootInfo pointer (passed by bootloader in RDI)
    
    ; Zero BSS
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor rax, rax
    rep stosb

    ; Reset stack pointer after BSS clear just in case
    mov rsp, [g_kernel_stack_top]
    and rsp, -16
    sub rsp, 8

    call syslog_init
    
    ; --- FIX: Pass BootInfo to paging_init ---
    mov rdi, r12
    call paging_init
    ; -----------------------------------------
    
    call gdt_init
    call interrupts_init

    ; Re-enable interrupts
    sti

    mov rdi, r12                ; restore BootInfo pointer for kmain
    call kmain

.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite align=1
