/* Linker script for a Unix kernel on i486 platform */
/* Assumes 32-bit ELF output for i486 architecture */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start) /* Entry point of the kernel, assuming _start is defined in your code */

/* Base address where the kernel will be loaded in virtual memory */
KERNEL_VMA = 0x7fc00000;

/* Memory layout */
SECTIONS
{
    /* Place the kernel at the virtual memory address */
    . = KERNEL_VMA;

    /* Multiboot header or other bootstrap code, if needed */
    .bootstrap :
    {
        *(.multiboot) /* For multiboot-compliant kernels, if applicable */
        *(.bootstrap)
    }

    /* Code section */
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }
    etext = .;

    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }
    edata = .;

    /* Uninitialized data (BSS) */
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Kernel end marker */
    end = .;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame)
        *(.rel.*)
        *(.rela.*)
    }
}

/* Define symbols for kernel boundaries */
_kernel_start = KERNEL_VMA;
_kernel_size = end - _kernel_start;
