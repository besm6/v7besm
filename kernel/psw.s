// ============================================================================
// The interrupt-enable bit
// ============================================================================
//
// void cli(void);    -- set БлПр: external interrupts blocked
// void sti(void);    -- clear БлПр: external interrupts delivered
// int  getpsw(void); -- read PSW back
//
// On the BESM-6 the global interrupt flag is the БлПр bit (02000) of PSW, the mode word
// M[021].  This is the kernel's INTERRUPT PRIORITY, not merely a guard around short
// critical sequences: setipl() in kernel/intr.c is built on these two, and the header there
// explains why БлПр and not МГРП carries the level.
//
// ONE INSTRUCTION EACH, because `уиа' (vtm) with REGISTER FIELD 0 is a mode-word write in
// supervisor mode: it takes БлП, БлЗ and БлПр straight from the address field and writes all
// three into PSW atomically (doc/Besm6_Instruction_Set.md, 024 VTM; doc/Memory_Mapping.md).
// It is a MASKED write -- ПоП, ПоК and the write-watch bit are not in the mask and do not
// move -- and it disturbs neither the accumulator nor ω, unlike the `ita 021'/`ati 021'
// read-modify-write this used to be.
//
// That it writes БлП and БлЗ as well is not a hazard here, it is the point: the kernel runs
// UNMAPPED WITH PROTECTION OFF (БлП = БлЗ = 1) as a standing invariant, so `02003'/`3' put
// back exactly what is already there and re-assert the invariant on the way past.
//
// THE PRECONDITION THAT BUYS: these two may only be called from ordinary unmapped kernel
// context.  Every caller is -- setipl() is the only one, reached from the C `spl*' family --
// and nothing inside a MAPPED bracket may call them: they would slam БлП back on and pull
// the mapping out from under the copy.  The brackets in uarea.S, seg.S and usermem.S issue
// their own `vtm 02002'/`vtm 02003' and bank PSW with `ita'/`ati' precisely because they must
// preserve a БлПр they do not know.  This file's job is the opposite one: to set it.
//
// Their own file, rather than besm6.S, for the usual reason: besm6.o cannot be linked into a
// standalone test -- its 0500 vector reaches into the C kernel and its _start seeds no stack
// -- and every test that links the real intr.o now needs cli/sti underneath it.

        .text

        .globl  cli
cli:                             // set БлПр -> external interrupts OFF
        vtm     02003            // PSW := БлП|БлЗ|БлПр (register field 0 = the mode write)
     13 uj

        .globl  sti
sti:                             // clear БлПр -> external interrupts ON
        vtm     3                // PSW := БлП|БлЗ, БлПр clear
     13 uj

// PSW, unlike РП and РЗ, CAN be read back, and this is the only way to say so in C.  The kernel
// itself never calls it -- it tracks the level in `curipl' (intr.c) and reads the interrupted mode
// word out of the trap frame -- so it is dropped from the image by the link-pull of libunix.a.  It
// exists for kernel/test/usys.c, which asserts from inside a sysent stub that the extracode gate
// really did open БлПр before dispatching (F_IPL).  Nothing else can check that: the level is a
// hardware bit, and every C-visible shadow of it would agree either way.
        .globl  getpsw
getpsw:
        ita     021              // A := PSW -- the result, in the accumulator
     13 uj
