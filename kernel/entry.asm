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

_start:
    ; Disable interrupts while the IDT is being built to avoid spurious faults
    ; on an uninitialized table.
    cli

    mov rbp, 0
    and rsp, -16              ; Align the stack to 16 bytes for System V ABI
    sub rsp, 8                ; Account for a return address push to keep 16-byte alignment before calls

    mov r12, rdi                ; preserve BootInfo pointer
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor rax, rax
    rep stosb

    call syslog_init
    call paging_init
    call gdt_init
    call interrupts_init

    ; Re-enable interrupts now that the IDT and PIC are configured.
    sti

    mov rdi, r12                ; restore BootInfo pointer
    call kmain

.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite align=1