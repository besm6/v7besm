/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// The shell's global variables -- the ones that had no home.
//
// This file has no v7 counterpart.  v7 declared these in defs.h, which every source
// includes, as TENTATIVE DEFINITIONS: `INT flags;' with no initializer and no extern.
// Twenty translation units each emitted a common-block symbol and the linker merged
// them.  C11 has no common block -- one object needs exactly one definition -- so the
// declarations in defs.h and stak.h became `extern' and the definitions came here.
//
// Only the homeless ones.  A global that already had a defining source keeps it, and
// defs.h names the file in a comment beside each: the messages and tables are still in
// msg.c, the name nodes in name.c, `output' and `standin' in main.c, `brkincr' in
// blok.c, `numbuf' in print.c, `flagadr' in args.c, `trapcom'/`trapflg' in fault.c.
//
// Two of these were defined TWICE in v7 and merged by the same mechanism: `nosubst'
// (io.c and service.c) and the stack's four pointers, which stak.h declared and no file
// defined at all.
//
#include "defs.h"

// temp files and io
INT ioset;
IOPTR iotemp; // files to be deleted sometime
IOPTR iopend; // documents waiting to be read at NL

// substitution
INT dolc;
STRING *dolv;
DOLPTR argfor;
ARGPTR gchain;

// words, as word() hands them to cmd()
INT wdval;
INT wdnum;
ARGPTR wdarg;
INT wdset;
BOOL reserv;

// special names -- $0, $?, $#, $! and $$
STRING cmdadr;
STRING exitadr;
STRING dolladr;
STRING pcsadr;
STRING pidadr;

// transput
STRING tmpnam;
INT serial;
INT peekc;
STRING comdiv;

//
// comdivset: whether -c was given.  v7 kept that fact in `comdiv' alone by writing
// `comdiv--' on the null pointer, so that a shell without -c held (char *)-1 and tested
// true.  Decrementing a null FAT pointer here underflows its byte-offset field into its
// word address and the result's truth value is compiler trivia, so the fact gets a flag
// of its own and comdiv stays a pointer throughout.
//
BOOL comdivset;

// set by trim(), read by copy() -- defined in both io.c and service.c in v7
BOOL nosubst;

// flags
INT flags;

// error exits from various parts of the shell
jmp_buf subshell;
jmp_buf errshell;

// fault handling
BOOL trapnote;

// execflgs
INT exitval;
BOOL execbrk;
INT loopcnt;
INT breakcnt;

//
// The expression stack (stak.h).  v7 declared all four in that header and defined none
// of them anywhere; only `stakbot' had a definition, in stak.c, and it keeps it.
//
BLKPTR stakbsy; // stack blocks covered by heap allocation, awaiting tdystak()
STKPTR stakbas; // base of the entire stack
STKPTR brkend;  // top of the entire stack -- as far as the break reaches
STKPTR staktop; // top of the current item
