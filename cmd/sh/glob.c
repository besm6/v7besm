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

// See defs.h, where each of these is declared and described.  Only the one-line
// reminders are repeated here.

// --- where the shell prints, and what it must clean up ---
INT ioset;    // a redirection has already claimed descriptor 0 for this command
IOPTR iotemp; // here-document temp files created so far, to be unlinked
IOPTR iopend; // here-documents promised on this line, whose text has not been read yet

// --- the positional parameters ---
INT dolc;      // $#
STRING *dolv;  // $0, $1, $2, ...
DOLPTR argfor; // parameter lists a running `for' loop is holding on to
ARGPTR gchain; // the argument words built so far for the command being assembled

// --- what the lexer last read ---
INT wdval;    // the symbol: 0 for an ordinary word, else a code from sym.h
INT wdnum;    // the digit in front of a redirection, as in `2>file'
ARGPTR wdarg; // the word itself, when wdval is 0
INT wdset;    // the word looks like `name=value'
BOOL reserv;  // the parser will accept a reserved word here

// --- the parameters that are not variables ---
STRING cmdadr;  // $0
STRING exitadr; // $?
STRING dolladr; // $#
STRING pcsadr;  // $!
STRING pidadr;  // $$

// --- reading input ---
STRING tmpnam; // where the serial number goes in tmpout[]
INT serial;    // counts here-documents, so each gets its own temp file
INT peekc;     // one character of lexer pushback
STRING comdiv; // the command string given with -c

//
// comdivset: whether -c was given.  v7 kept that fact in `comdiv' alone by writing
// `comdiv--' on the null pointer, so that a shell without -c held (char *)-1 and tested
// true.  Decrementing a null FAT pointer here underflows its byte-offset field into its
// word address and the result's truth value is compiler trivia, so the fact gets a flag
// of its own and comdiv stays a pointer throughout.
//
BOOL comdivset;

// Set by trim(): the string it just processed contained a quoted character.  Defined in
// both io.c and service.c in v7.
BOOL nosubst;

// Every option in force, one bit each -- see the noexec/intflg/... list in defs.h.
INT flags;

// --- the two long jumps (defs.h) ---
jmp_buf subshell; // back to the top of main(), when a file turns out to be a script
jmp_buf errshell; // back to the prompt, from any fatal error

// Some signal has fired and has not been dealt with yet.
BOOL trapnote;

// --- how the tree walker keeps its place ---
INT exitval;  // the last command's exit status
BOOL execbrk; // `break'/`continue' asked to stop walking
INT loopcnt;  // how many loops are running
INT breakcnt; // how many more levels `break N' has to unwind

//
// The expression stack (stak.h).  v7 declared all four in that header and defined none
// of them anywhere; only `stakbot' had a definition, in stak.c, and it keeps it.
//
BLKPTR stakbsy; // stack blocks covered by heap allocation, awaiting tdystak()
STKPTR stakbas; // base of the entire stack
STKPTR brkend;  // top of the entire stack -- as far as the break reaches
STKPTR staktop; // top of the current item
