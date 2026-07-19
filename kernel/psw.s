// ============================================================================
// The interrupt-enable bit
// ============================================================================
//
// void cli(void);    -- set БлПр: external interrupts blocked
// void sti(void);    -- clear БлПр: external interrupts delivered
//
// On the BESM-6 the global interrupt flag is the БлПр bit (02000) of ПСВ, the mode word
// M[021].  This is the kernel's INTERRUPT PRIORITY, not merely a guard around short
// critical sequences: setipl() in kernel/intr.c is built on these two, and the header there
// explains why БлПр and not МГРП carries the level.
//
// Read-modify-write, never `vtm': a `vtm N,0' writes БлП, БлЗ and БлПр together from its
// address field, so it would clobber the mapping and protection overrides along with the
// bit we mean to change.  `ita'/`ati' take a 5-bit register number in supervisor mode, so
// M[021] is reachable (doc/Memory_Mapping.md).
//
// Their own file, rather than besm6.S, for the usual reason: besm6.o cannot be linked into a
// standalone test -- its 0500 vector reaches into the C kernel and its _start seeds no stack
// -- and every test that links the real intr.o now needs cli/sti underneath it.

        .text

        .globl  cli
cli:                             // set БлПр -> external interrupts OFF
        ita     021              // A := ПСВ
        aox     #02000           //   set БлПр
        ati     021              // ПСВ := A
     13 uj

        .globl  sti
sti:                             // clear БлПр -> external interrupts ON
        ita     021              // A := ПСВ
        aax     #075777          //   clear БлПр (077777 & ~02000), keep the other bits
        ati     021              // ПСВ := A
     13 uj
