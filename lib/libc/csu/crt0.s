//
// crt0.s -- C startup for a BESM-6 Unix program.  Link it FIRST, so that _start
// is the executable's entry point.
//
// Nothing is handed over in a register.  Both gates -- exece() in kernel/sys1.c and
// Machine::exec() in cmd/sim/machine.cpp -- lay the argument block at the fixed base
// 070000 and zero every register but r15:
//
//      070000  argc
//              argv[0] .. argv[argc-1]     char * fat pointers
//              0
//              envp[0] .. envp[ne-1]
//              0
//              the strings, byte-packed six to a word
//        r15 = the first free word above the block
//
// So argc is at an absolute address this file can simply name, argv[] begins one
// word later, and envp[] begins past the argv[] terminator -- the only thing to
// compute.  The stack pointer is already seeded and must not be touched.
//
// Every slot in the two vectors strides by one word: `char *' is a fat pointer and
// carries a byte offset, but `char **' is a plain word address, which is what argv
// and envp are here.
//
// 070000 needs no address extension.  A short address field reaches [0..07777] and
// [070000..077777] -- the segment bit is worth exactly +070000 -- so `xta 070000' is
// one instruction.  The bss references do need the `< >' escape, whose `utc' carries
// the full 15 bits: this file links low today, but the segment order is
// const|text|data|bss and a large program would push `environ' out of the 12-bit field.
//
        .text
        .globl  _start, environ

ARGC    = 070000                // where the gate puts argc
ARGV    = 070001                // ...and the first argv[] slot

_start:
        ntr     7               // R = 7: NTR 3 suppression plus logical omega,
                                //   the mode word every b$ helper expects.
                                //   See doc/Besm6_Runtime_Library.md.

// environ = &block[argc + 2]: past argc itself, the argc pointers, and the null
// that terminates the argv[] vector.  r8 is scratch -- only r1-r7 are callee-saved,
// and there is no caller.
        xta     ARGC            // ACC = argc
        ati     010             // r8 = argc
      8 utm     ARGV+1          // r8 = argc + 070002 = &envp[0]
        ita     010             // ACC = envp
        atx     <environ>

// main(argc, argv, envp): arguments pushed in direct order, the last one left in
// the accumulator, r14 = -3.  `#ARGV' is the pooled constant 070001 -- the ADDRESS
// of the vector, not a word read from it.
        xta     ARGC
        xts     #ARGV
        xts     <environ>
     14 vtm     -3
     13 vjm     main

// main's status is already in the accumulator, which is where exit's one argument
// belongs.  exit does not return, so this is a tail jump and nothing follows.
     14 vtm     -1
        uj      exit

        .bss
environ:
        . = . + 1
