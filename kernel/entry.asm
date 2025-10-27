
BITS 64

section .text
    global _start
    extern kmain

_start:
    mov rbp, 0
    call kmain

.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite align=1
