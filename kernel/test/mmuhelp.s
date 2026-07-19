//
// mmuhelp.s -- reach a virtual address from mmutest.
//
// The kernel runs unmapped (БлП set), so its addresses ARE physical.  To touch a
// page through the process's map it clears БлП for the length of one access: the
// user's РП is loaded, data addresses go through it, and instruction fetch carries
// on unmapped because supervisor fetch is NEVER mapped.  That asymmetry is what
// makes copyin/copyout free on this machine (doc/Memory_Mapping.md, "Why the
// asymmetry is the key to the whole machine").
//
// `vtm <mask>' with register field 0 in supervisor mode writes the mode bits
// straight from the address field: 2 = БлЗ alone (mapping ON, protection off), 3 =
// БлП|БлЗ (mapping off) -- the mode the kernel normally runs in, and the one crt0
// sets.  Protection stays off throughout: it is the kernel doing the touching.
//
// Everything between the two `vtm's runs out of registers.  While mapping is on the
// kernel's own data is not addressable -- the addresses mean something else -- so
// there is nothing here but the one load or store.  This is the shape the real
// copyin/copyout use; see kernel/usermem.s.

        .text

// unsigned peek(unsigned vaddr) -- read one word through the process's map.
// One parameter: vaddr arrives in the accumulator.
        .globl  peek
peek:   ati     016              // r14 = vaddr
        vtm     2                // clear БлП: data now goes through РП
     14 xta                      // A = the process's word at vaddr
        vtm     3                // set БлП: back to physical addressing
     13 uj

// void poke(unsigned vaddr, unsigned val) -- write one word through the map.
// Two parameters: vaddr is pushed, val arrives in the accumulator.
        .globl  poke
poke:   atx     pv               // stash val -- still unmapped, so this is our word
     15 xta     -1               // A = vaddr, the pushed argument
        ati     016              // r14 = vaddr
        xta     pv               // A = val
        vtm     2                // clear БлП
     14 atx                      // store it through the process's map
        vtm     3                // set БлП
     15 utm     -1               // pop the pushed argument
     13 uj

        .bss
pv:     . = . + 1
