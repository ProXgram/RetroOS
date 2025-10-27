
BITS 16
ORG 0x7C00

%ifndef TOTAL_SECTORS
%error "TOTAL_SECTORS must be defined at assembly time"
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    mov si, disk_address_packet
    mov word [si + 2], TOTAL_SECTORS
    mov word [si + 4], stage2_offset
    mov word [si + 6], 0x0000
    mov dword [si + 8], 1
    mov dword [si + 12], 0

    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    jmp 0x0000:stage2_offset

disk_error:
    mov si, disk_error_message
.print_loop:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .print_loop
.halt:
    cli
    hlt
    jmp .halt

boot_drive: db 0
stage2_offset equ 0x7E00

disk_address_packet:
    db 0x10
    db 0x00
    dw 0x0000
    dw stage2_offset
    dw 0x0000
    dq 1

disk_error_message db 'Disk read error!', 0

TIMES 510 - ($ - $$) db 0
dw 0xAA55
