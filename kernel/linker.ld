ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text : ALIGN(0x1000)
    {
        *(.text*)
    }

    .rodata : ALIGN(0x1000)
    {
        *(.rodata*)
    }

    .data : ALIGN(0x1000)
    {
        *(.data*)
    }

    .bss : ALIGN(0x1000)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_end = .;
}

ASSERT(__kernel_end <= 0x001F0000,
       "Kernel image exceeds safe limit and would collide with the long mode stack");
