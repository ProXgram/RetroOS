%ifndef KERNEL_SIZE_BYTES
%error "KERNEL_SIZE_BYTES must be defined at assembly time"
%endif

BITS 16
ORG 0x7E00

CODE32_SEG     equ 0x08
DATA_SEG       equ 0x10
CODE64_SEG     equ 0x18
KERNEL_DEST    equ 0x00100000

%ifndef PROTECTED_STACK
PROTECTED_STACK equ 0x00280000
%endif

%ifndef LONG_STACK_TOP
LONG_STACK_TOP  equ 0x003FF000
%endif

; Paging Structures
PML4            equ 0x00200000
PDPT            equ 0x00201000
PD              equ 0x00202000
PD_HIGH         equ 0x00203000

BOOT_INFO       equ 0x00005000

; VESA VBE Structures
VBE_INFO_ADDR   equ 0x00006000
MODE_INFO_ADDR  equ 0x00006200

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7E00

    ; --- VESA VBE SETUP ---
    ; 1. Get VBE Controller Info
    mov di, VBE_INFO_ADDR
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    ; 2. Find 1024x768x32 mode
    ; We iterate through the mode list provided by VBE
    mov fs, word [VBE_INFO_ADDR + 16]
    mov si, word [VBE_INFO_ADDR + 14] ; FS:SI points to mode list

.find_mode:
    mov cx, [fs:si]
    cmp cx, 0xFFFF      ; End of list
    je .vbe_fail
    add si, 2

    ; Get Mode Info
    mov ax, 0x4F01
    mov di, MODE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .find_mode

    ; Check Resolution (1024x768)
    mov ax, [MODE_INFO_ADDR + 18] ; X Resolution
    cmp ax, 1024
    jne .find_mode
    mov ax, [MODE_INFO_ADDR + 20] ; Y Resolution
    cmp ax, 768
    jne .find_mode
    
    ; Check BPP (32)
    mov al, [MODE_INFO_ADDR + 25]
    cmp al, 32
    jne .find_mode

    ; Check for Linear Framebuffer support (Bit 7 of ModeAttributes)
    mov ax, [MODE_INFO_ADDR + 0]
    and ax, 0x0080
    jz .find_mode

    ; Found it! Set the mode (CX holds mode number)
    mov bx, cx
    or bx, 0x4000       ; Set Linear Framebuffer bit
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    ; --- Populate BootInfo for Kernel ---
    ; Width
    mov ax, [MODE_INFO_ADDR + 18]
    mov dword [BOOT_INFO + 0], 0
    mov word [BOOT_INFO + 0], ax
    
    ; Height
    mov ax, [MODE_INFO_ADDR + 20]
    mov dword [BOOT_INFO + 4], 0
    mov word [BOOT_INFO + 4], ax
    
    ; Pitch (Bytes per line)
    mov ax, [MODE_INFO_ADDR + 16]
    mov dword [BOOT_INFO + 8], 0
    mov word [BOOT_INFO + 8], ax
    
    ; BPP
    xor ax, ax
    mov al, [MODE_INFO_ADDR + 25]
    mov dword [BOOT_INFO + 12], eax
    
    ; Framebuffer Address (PhysBasePtr)
    mov eax, [MODE_INFO_ADDR + 40]
    mov dword [BOOT_INFO + 16], eax   ; Low 32 bits
    mov dword [BOOT_INFO + 20], 0     ; High 32 bits (assuming < 4GB for now)

    jmp .enable_pm

.vbe_fail:
    ; Fallback if graphics fail (hlt loop)
    cli
    hlt
    jmp .vbe_fail

.enable_pm:
    call enable_a20
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    and eax, ~(1 << 2)
    or eax, (1 << 1)
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

    ; --- Paging Setup (Identity Map First 1GB) ---
    mov edi, PML4
    xor eax, eax
    mov ecx, 4096
    rep stosd ; Zero all tables

    ; Link Tables
    mov eax, PDPT | 0x3
    mov [PML4], eax
    mov eax, PD | 0x3
    mov [PDPT], eax
    mov eax, PD_HIGH | 0x3
    mov [PDPT + 24], eax

    ; Identity Map (Low 1GB)
    mov ecx, 512
    xor eax, eax
    mov edi, PD
.map_low:
    mov edx, eax
    or edx, 0x00000083 ; Present | RW | HugePage
    mov [edi], edx
    add eax, 0x00200000
    add edi, 8
    loop .map_low

    ; Map Framebuffer (if > 1GB)?
    ; For simplicity, we assume the FB is within the 32-bit address space 
    ; which the 1GB identity map + huge pages might cover, OR we rely on 
    ; the fact that we are in long mode and can access physical RAM directly 
    ; if mapped. 
    ; *CRITICAL FIX*: A 1024x768 buffer is ~3MB. It often lives at 0xE0000000 
    ; or similar high addresses (above RAM). 
    ; We MUST map the higher memory ranges or simply identity map the first 4GB 
    ; using PDPT entries to cover typical MMIO space.
    
    ; Let's Map 4GB Identity to be safe for VESA LFB
    ; PDPT[0] covers 0-1GB (Already done via PD)
    ; We need PDPT[1], [2], [3] pointing to more PDs? 
    ; Or simpler: Just use 1GB for now. If VESA fails, we need a better pager.
    ; Most QEMU LFB is at 0xFD000000. We need to map that.
    
    ; Quick fix: Map the 3rd GB and 4th GB roughly using 1GB huge pages (PDPTE PS bit)
    ; Note: PDPTE with PS bit (bit 7) maps 1GB directly.
    
    ; Map 0-1GB (Fine grained via PD, done above)
    
    ; Map 1GB-512GB using 1GB Huge Pages in PDPT?
    ; Let's map the high MMIO space (3GB-4GB)
    
    mov edi, PDPT
    ; PDPT[3] -> Covers 3GB-4GB (0xC0000000 - 0xFFFFFFFF)
    ; This usually covers the LFB in QEMU.
    mov eax, 0xC0000000
    or eax, 0x83 ; Present | RW | Huge (1GB page)
    mov [edi + 24], eax 

    mov eax, PML4
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10) 
    mov cr4, eax

    fninit
    mov eax, cr0
    and eax, ~((1 << 2) | (1 << 3))
    or eax, (1 << 1)
    or eax, (1 << 31) ; Paging Enable
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
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF ; 32-bit Code
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF ; 32-bit Data
    dw 0x0000, 0x0000, 0x9A00, 0x0020 ; 64-bit Code
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

stage2_end:
