
%ifndef KERNEL_SIZE_BYTES
%error "KERNEL_SIZE_BYTES must be defined at assembly time"
%endif

BITS 16
ORG 0x7E00

CODE32_SEG equ 0x08
DATA_SEG  equ 0x10
CODE64_SEG equ 0x18
KERNEL_DEST equ 0x00100000
PROTECTED_STACK equ 0x0009F000
LONG_STACK_TOP equ 0x001FF000
PML4 equ 0x00009000
PDPT equ 0x0000A000
PD   equ 0x0000B000
PD_HIGH equ 0x0000C000
BOOT_INFO equ 0x00005000

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7E00

    mov ax, 0x0003
    int 0x10

    mov dword [BOOT_INFO + 0], 80
    mov dword [BOOT_INFO + 4], 25
    mov dword [BOOT_INFO + 8], 160
    mov dword [BOOT_INFO + 12], 16
    mov dword [BOOT_INFO + 16], 0x000B8000
    mov dword [BOOT_INFO + 20], 0

    call enable_a20

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1               ; enable protected mode
    and eax, ~(1 << 2)        ; clear EM to allow FPU/SSE instructions
    or eax, (1 << 1)          ; ensure MP is set for proper FPU error reporting
    mov cr0, eax
    jmp CODE32_SEG:protected_mode_entry

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

[BITS 32]
protected_mode_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, PROTECTED_STACK

    cld
    mov esi, stage2_end
    mov edi, KERNEL_DEST
    mov ecx, KERNEL_SIZE_BYTES
    rep movsb

    mov edi, PML4
    xor eax, eax
    mov ecx, 4096 / 4
    rep stosd

    mov edi, PDPT
    xor eax, eax
    mov ecx, 4096 / 4
    rep stosd

    mov edi, PD
    xor eax, eax
    mov ecx, 4096 / 4
    rep stosd

    mov edi, PD_HIGH
    xor eax, eax
    mov ecx, 4096 / 4
    rep stosd

    mov eax, PDPT | 0x3
    mov [PML4], eax
    mov dword [PML4 + 4], 0

    mov eax, PD | 0x3
    mov [PDPT], eax
    mov dword [PDPT + 4], 0

    mov eax, PD_HIGH | 0x3
    mov [PDPT + 24], eax
    mov dword [PDPT + 28], 0

    mov ecx, 512
    xor eax, eax
    mov edi, PD
.map_low:
    mov edx, eax
    or edx, 0x00000083
    mov [edi], edx
    mov dword [edi + 4], 0
    add eax, 0x00200000
    add edi, 8
    loop .map_low

    mov edi, PD_HIGH
    mov eax, 0xC0000083
    mov [edi + (0 * 8)], eax
    mov dword [edi + (0 * 8) + 4], 0
    mov eax, 0xC0020083
    mov [edi + (1 * 8)], eax
    mov dword [edi + (1 * 8) + 4], 0
    mov eax, 0xC0040083
    mov [edi + (2 * 8)], eax
    mov dword [edi + (2 * 8) + 4], 0
    mov eax, 0xC0060083
    mov [edi + (3 * 8)], eax
    mov dword [edi + (3 * 8) + 4], 0

    mov eax, PML4
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10) ; enable PAE and SSE support
    mov cr4, eax

    fninit                          ; make sure the FPU/SSE state is clean
    mov eax, cr0
    and eax, ~((1 << 2) | (1 << 3)) ; clear EM/TS so FP instructions work
    or eax, (1 << 1)                ; set MP for proper FPU error reporting
    mov cr0, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp CODE64_SEG:long_mode_entry

[BITS 64]
long_mode_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, LONG_STACK_TOP
    xor rbp, rbp

    mov rdi, BOOT_INFO
    mov rax, KERNEL_DEST
    call rax

.hang:
    hlt
    jmp .hang

[BITS 16]
gdt_start:
    dq 0

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

    dw 0x0000
    dw 0x0000
    db 0x00
    db 10011010b
    db 00100000b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

stage2_end:
