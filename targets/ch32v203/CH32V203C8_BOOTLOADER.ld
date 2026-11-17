ENTRY(_start)

/* SDK execution mapping. Programming and protocol addresses remain 0x08000000. */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 24K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x4F00
    REQUEST (rw) : ORIGIN = 0x20004F00, LENGTH = 0x100
}

SECTIONS
{
    .init :
    {
        KEEP(*(.init))
        . = ALIGN(4);
    } > FLASH

    .vector :
    {
        KEEP(*(.vector))
        . = ALIGN(64);
    } > FLASH

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .data :
    {
        _data_vma = .;
        __global_pointer$ = . + 0x800;
        *(.data .data.* .sdata .sdata.* .srodata .srodata.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH
    _data_lma = LOADADDR(.data);
    __flash_end = LOADADDR(.data) + SIZEOF(.data);

    .bss (NOLOAD) :
    {
        _sbss = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .stack ORIGIN(RAM) + LENGTH(RAM) - 4096 (NOLOAD) :
    {
        _susrstack = .;
        . += 4096;
        _eusrstack = .;
    } > RAM

    .boot_request (NOLOAD) :
    {
        __boot_request = .;
        . += 0x100;
    } > REQUEST

    /DISCARD/ : { *(.comment) *(.eh_frame*) }

    ASSERT(ORIGIN(FLASH) == 0, "SDK execution alias must be zero")
    ASSERT(_start == ORIGIN(FLASH), "entry must be at the SDK zero execution alias")
    ASSERT(ADDR(.vector) == ORIGIN(FLASH) + 4, "SDK vector offset changed")
    ASSERT(__flash_end <= ORIGIN(FLASH) + LENGTH(FLASH), "24 KiB boot region exceeded")
    ASSERT(_ebss <= _susrstack, "RAM overlaps 4 KiB stack")
}
