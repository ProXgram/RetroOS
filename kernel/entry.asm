
BITS 64

section .text
    global _start
    extern kmain
    extern interrupts_init
    extern __bss_start
    extern __bss_end

_start:
    mov rbp, 0

    mov r12, rdi                ; preserve BootInfo pointer
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor rax, rax
    rep stosb

    call interrupts_init

    mov rdi, r12                ; restore BootInfo pointer
    call kmain

.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite align=1
