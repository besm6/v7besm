Port of Unix v7 to BESM-6 mainframe.

We already have a cross-compiler for BESM-6: https://github.com/besm6/c-compiler/

There is also an authentic simulator of the BESM-6 hardware: https://github.com/besm6/simh/tree/master/BESM6/

We use the above software to port Unix v7 to BESM-6.

The nearest goals:
 * Create assembler in AT&T style with Madlen mnemonics (port from Elbrous-B)
 * Create linker (port from Elbrous-B)
 * Create libc library
 * Build and link the kernel
 * Develop drivers of required peripherals for the kernel
