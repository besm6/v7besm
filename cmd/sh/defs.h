/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The UNIX shell -- S. R. Bourne, Bell Telephone Laboratories.
//
// WHAT CHANGED, AND WHY.  Two things, and they are worth separating.
//
// C11.  b6parse has no implicit int, no K&R parameter lists and no untyped `register
// i;', so every function here has a prototype and an explicit return type.  v7's
// ALGOL-68 macro dialect (mac.h: IF/THEN/FI, LOOP/POOL, REP/PER/DONE, SWITCH/IN/ENDSW)
// is gone with it -- not because C11 rejects it, it preprocesses fine, but because the
// pointer fixes below have to be read and reviewed, and they cannot be while the
// control flow is spelled in macros clang-format cannot format.
//
// And this header no longer DEFINES the shell's globals.  v7 declared them here, in a
// header every file includes, as tentative definitions -- `INT flags;' -- and let the
// linker merge the twenty copies into one common block.  C11 has no such thing.  They
// are extern here and defined once in glob.c.
//
// THE MACHINE.  A BESM-6 word is 48 bits and the machine is word-addressed:
// sizeof(int) == 6 char-units == one word, `char' is unsigned, `char *' and `void *'
// are FAT pointers (a bit-48 marker, a 3-bit byte offset, a 15-bit word address) and
// every other pointer is a bare word address in bits 15-1.  See
// doc/Besm6_Data_Representation.md sections 7-8.  Three consequences run through this
// program, because the v7 shell is written for a byte-addressed machine on which an
// `int' and a `char *' are the same thing:
//
//   - A FLAG PACKED INTO A POINTER GOES IN BIT 16, NEVER IN BIT 0.  Bit 0 of a word
//     address names the neighbouring WORD.  blok.c's BUSY and service.c's ARGMK both
//     moved; lib/libc/gen/malloc.c is v7's same allocator with the same fix and carries
//     the full reasoning.  v7's Lcheat/Rcheat puns, which existed only to do this, are
//     gone from mode.h.
//   - ROUNDING TO A WORD IS NOT A BIT MASK.  BYTESPERWORD is 6, and `& ~(6-1)' is not a
//     rounding operation at all -- it rounds 7 to 8.  v7's round() is replaced by the
//     two macros below, one for byte counts and one for pointers.
//   - EVERY CAST FROM char * TO A NODE POINTER FLOORS.  It does not round: it drops the
//     byte offset and keeps the word.  So the whole parse tree and argument list, which
//     the shell builds by casting stack addresses to ARGPTR/COMPTR/STRING *, depends on
//     an invariant that has to hold and be stated: STAKBOT, STAKTOP AFTER endstak() OR
//     getstak(), AND EVERY shalloc() RESULT SIT AT BYTE #0 OF A WORD.  wordup() below
//     and sizeup() are what keep it.
//
#ifndef SH_DEFS_H
#define SH_DEFS_H

#include <setjmp.h>
#include <sys/param.h>
#include <sys/types.h>

// error exits from various parts of shell
#define ERROR   1
#define SYNBAD  2
#define SIGFAIL 3
#define SIGFLG  0200

// command tree
#define FPRS   020
#define FINT   040
#define FAMP   0100
#define FPIN   0400
#define FPOU   01000
#define FPCL   02000
#define FCMD   04000
#define COMMSK 017

#define TCOM  0
#define TPAR  1
#define TFIL  2
#define TLST  3
#define TIF   4
#define TWH   5
#define TUN   6
#define TSW   7
#define TAND  8
#define TORF  9
#define TFORK 10
#define TFOR  11

// execute table
#define SYSSET    1
#define SYSCD     2
#define SYSEXEC   3
#define SYSLOGIN  4
#define SYSTRAP   5
#define SYSEXIT   6
#define SYSSHFT   7
#define SYSWAIT   8
#define SYSCONT   9
#define SYSBREAK  10
#define SYSEVAL   11
#define SYSDOT    12
#define SYSRDONLY 13
#define SYSTIMES  14
#define SYSXPORT  15
#define SYSNULL   16
#define SYSREAD   17
#define SYSTST    18
#define SYSUMASK  19

// used for input and output of shell
#define INIO 10
#define OTIO 11

// io nodes
#define USERIO 10
#define IOUFD  15
#define IODOC  16
#define IOPUT  32
#define IOAPP  64
#define IOMOV  128
#define IORDW  256
#define INPIPE 0
#define OTPIPE 1

// arg list terminator
#define ENDARGS 0

//
// The dialect mac.h used to spell.  Only the value macros survive; the control-flow ones
// are written out at their call sites.
//
#define TRUE   (-1)
#define FALSE  0
#define LOBYTE 0377
#define STRIP  0177
#define QUOTE  0200

// SHEOF, not EOF: <stdio.h> in this tree spells EOF as -1, and if it were ever reached
// the include order would decide whether word() can end a script at all.
#define SHEOF 0
#define NL    '\n'
#define SP    ' '
#define LQ    '`'
#define RQ    '\''
#define MINUS '-'
#define COLON ':'

#include "mode.h"
#include "name.h"

#define attrib(n, f) (n->namflg |= f)
#define closepipe(x) (close(x[INPIPE]), close(x[OTPIPE]))
#define eq(a, b)     (cf(a, b) == 0)
#define max(a, b)    ((a) > (b) ? (a) : (b))

//
// Round a byte count up to a whole number of words.  v7 wrote round(n,BYTESPERWORD) as
// a bit mask, which needs a power-of-two modulus; here the modulus is 6.
//
#define sizeup(n) ((((n) + BYTESPERWORD - 1) / BYTESPERWORD) * BYTESPERWORD)

//
// Round a char * up to the next word boundary -- the other half of v7's round(), and
// the one that keeps the invariant this header opens with.
//
// It works BECAUSE the cast floors.  char * -> int * drops the fat pointer's byte
// offset and keeps its word, so adding BYTESPERWORD-1 first and flooring afterwards is
// exactly a ceiling; casting back yields a fat pointer at byte #0 of that word.  No
// arithmetic on the pointer's integer value is involved, which is the point: there is
// none that would be correct.
//
#define wordup(p) ((STKPTR)(INT *)((p) + (BYTESPERWORD - 1)))

// stack
#define BLK(x) ((BLKPTR)(x))
#define BYT(x) ((BYTPTR)(x))
#define STK(x) ((STKPTR)(x))
#define ADR(x) ((char *)(x))

#include "stak.h"

//
// The linker's end-of-bss symbol (b6ld defines it, cmd/ld/ld.c), where the arena
// starts.  Declared as an array, so that its decay produces a genuine fat pointer at
// offset 0 rather than a cast -- lib/libc/sys/sbrk.c declares it the same way and for
// the same reason.  v7 wrote `address end[]', a tentative definition of a LINKER symbol
// in a header; left that way the shell would define an object of its own and blok.c's
// arena would start in the wrong place.
//
extern char end[];

// ======== the globals, defined in glob.c unless noted ========

// temp files and io
extern UFD output; // main.c
extern INT ioset;
extern IOPTR iotemp; // files to be deleted sometime
extern IOPTR iopend; // documents waiting to be read at NL

// substitution
extern INT dolc;
extern STRING *dolv;
extern DOLPTR argfor;
extern ARGPTR gchain;

// string constants -- msg.c
extern MSG atline;
extern MSG readmsg;
extern MSG colon;
extern MSG minus;
extern MSG nullstr;
extern MSG sptbnl;
extern MSG unexpected;
extern MSG endoffile;
extern MSG synmsg;

// name tree and words
extern SYSNOD reserved[]; // msg.c
extern SYSNOD commands[]; // msg.c
extern INT wdval;
extern INT wdnum;
extern ARGPTR wdarg;
extern INT wdset;
extern BOOL reserv;

// prompting -- msg.c
extern MSG stdprompt;
extern MSG supprompt;
extern MSG profile;

// built in names -- name.c
extern NAMNOD fngnod;
extern NAMNOD ifsnod;
extern NAMNOD homenod;
extern NAMNOD mailnod;
extern NAMNOD pathnod;
extern NAMNOD ps1nod;
extern NAMNOD ps2nod;

// special names
extern MSG flagadr; // args.c
extern STRING cmdadr;
extern STRING exitadr;
extern STRING dolladr;
extern STRING pcsadr;
extern STRING pidadr;

extern MSG defpath; // msg.c

// names always present -- msg.c
extern MSG mailname;
extern MSG homename;
extern MSG pathname;
extern MSG fngname;
extern MSG ifsname;
extern MSG ps1name;
extern MSG ps2name;

// transput
extern CHAR tmpout[]; // main.c
extern STRING tmpnam;
extern INT serial;
#define TMPNAM 7
extern SHFILE standin; // main.c
#define input (standin->fdes)
#define eof   (standin->feof)
extern INT peekc;
extern STRING comdiv;
extern BOOL comdivset; // see main.c: v7 decremented the null comdiv instead
extern MSG devnull;    // msg.c
extern BOOL nosubst;   // set by trim()

// flags
#define noexec  01
#define intflg  02
#define prompt  04
#define setflg  010
#define errflg  020
#define ttyflg  040
#define forked  0100
#define oneflg  0200
#define rshflg  0400
#define waiting 01000
#define stdflg  02000
#define execpr  04000
#define readpr  010000
#define keyflg  020000
extern INT flags;

// error exits from various parts of shell
extern jmp_buf subshell;
extern jmp_buf errshell;

// fault handling
#include "brkincr.h"
extern POS brkincr; // blok.c

#define MINTRAP 0
#define MAXTRAP 17

#define INTR    2
#define QUIT    3
#define MEMF    11
#define ALARM   14
#define KILL    15
#define TRAPSET 2
#define SIGSET  4
#define SIGMOD  8

extern BOOL trapnote;
extern STRING trapcom[]; // fault.c
extern BOOL trapflg[];   // fault.c

// name tree and words

// Room for the decimal form of an INT: thirteen digits of a 41-bit signed value, a sign
// and a NUL.  v7's numbuf was six bytes, which was all a 16-bit int ever needed.
#define NUMBUF 16
extern CHAR numbuf[];   // print.c
extern MSG export;      // msg.c
extern MSG readonly;    // msg.c
extern STRING sysmsg[]; // msg.c

// execflgs
extern INT exitval;
extern BOOL execbrk;
extern INT loopcnt;
extern INT breakcnt;

// messages -- msg.c
extern MSG mailmsg;
extern MSG coredump;
extern MSG badopt;
extern MSG badparam;
extern MSG badsub;
extern MSG nospace;
extern MSG notfound;
extern MSG badtrap;
extern MSG baddir;
extern MSG badshift;
extern MSG illegal;
extern MSG restricted;
extern MSG execpmsg;
extern MSG notid;
extern MSG wtfailed;
extern MSG badcreate;
extern MSG piperr;
extern MSG badopen;
extern MSG badnum;
extern MSG arglist;
extern MSG txtbsy;
extern MSG toobig;
extern MSG badexec;
extern MSG badfile;

// ======== the functions ========

// args.c
INT options(INT argc, STRING *argv);
void setargs(STRING argi[]);
DOLPTR freeargs(DOLPTR blk);
void clearup(void);
DOLPTR useargs(void);

//
// blok.c.  v7 named these alloc()/free() and then `#define alloc malloc' in this
// header, so the shell's own arena DEFINED malloc and free.  That works only as long as
// nothing drags libc's malloc.o into the link, and half of stdio references it.  The
// shell keeps its own allocator under its own names instead, and libc's is free to
// coexist unreferenced.
//
ADDRESS shalloc(POS nbytes);
void addblok(POS reqd);
void shfree(BLKPTR ap);

// builtin.c
INT builtin(INT argn, STRING *com);

// cmd.c
TREPTR makefork(INT flgs, TREPTR i);
TREPTR cmd(INT sym, INT flg);

// error.c
void exitset(void);
void sigchk(void);
_Noreturn void failed(STRING s1, STRING s2);
_Noreturn void error(STRING s);
_Noreturn void exitsh(INT xno);
_Noreturn void done(void);
void rmtemp(STKPTR base);

// expand.c
INT expand(STRING as, INT rflg);
INT gmatch(STRING s, STRING p);
void makearg(STRING args);

// fault.c
void fault(INT sig);
void stdsigs(void);
INT ignsig(INT n);
void getsig(INT n);
void oldsigs(void);
void clrsig(INT i);
void chktrap(void);

// io.c
void initf(UFD fd);
INT estabf(STRING s);
void push(SHFILE af);
INT pop(void);
void chkpipe(INT *pv);
INT chkopen(STRING idf);
void shrename(INT f1, INT f2); // ISO C owns the name `rename'
INT create(STRING s);
INT tmpfil(void);
void copy(IOPTR ioparg);

// macro.c
STRING macro(STRING as);
void subst(INT in, INT ot);

// main.c
void chkpr(CHAR eor);
void settmp(void);

// name.c
INT syslook(STRING w, SYSPTR syswds);
void setlist(ARGPTR arg, INT xp);
void setname(STRING argi, INT xp);
void replace(STRING *a, STRING v);
void dfault(NAMPTR n, STRING v);
void assign(NAMPTR n, STRING v);
INT readvar(STRING *names);
void assnum(STRING *p, INT i);
STRING make(STRING v);
NAMPTR lookup(STRING nam);
void namscan(void (*fn)(NAMPTR));
void printnam(NAMPTR n);
void exname(NAMPTR n);
void printflg(NAMPTR n);
void readenv(void);  // v7 called this getenv(); libc owns that name
STRING *shenv(void); // ... and this one setenv()
void countnam(NAMPTR n);
void pushnam(NAMPTR n);

// print.c
void newline(void);
void blank(void);
void prp(void);
void prs(STRING as);
void prc(CHAR c);
void prt(L_INT t);
void prn(INT n);
void itos(INT n);
INT stoi(STRING icp);

// service.c
void initio(IOPTR iop);
STRING getpath(STRING s);
INT pathopen(STRING path, STRING name);
STRING catpath(STRING path, STRING name);
void execa(STRING at[]);
void postclr(void);
void post(INT pcsid);
void await(INT i);
void trim(STRING at);
STRING mactrim(STRING s);
STRING *scan(INT argn);
INT getarg(COMPTR ac);

// setbrk.c
INT setbrk(INT incr);

// stak.c
STKPTR getstak(INT asize);
STKPTR locstak(void);
STKPTR savstak(void);
STKPTR endstak(STRING argp);
void tdystak(STKPTR x);
void stakchk(void);
STKPTR cpystak(STKPTR x);

// string.c
STRING movstr(STRING a, STRING b);
INT any(CHAR c, STRING s);
INT cf(STRING s1, STRING s2);
INT length(STRING as);

// word.c
INT word(void);
INT nextc(CHAR quote);
INT readc(void);

//
// xec.c.  execute() is v7's one genuinely variadic function: it is written with four
// parameters and called with two, three or four, which K&R allowed and C11 does not.
// It takes four now, and the pipe-less call sites pass NULL twice.
//
INT execute(TREPTR argt, INT execflg, INT *pf1, INT *pf2);

//
// execexp() had the same shape in miniature: v7 declared its second parameter a file
// descriptor and then passed it a STRING * from the `eval' builtin, laundering a
// pointer through an int.  The two uses are separate parameters here.
//
void execexp(STRING s, UFD fd, STRING *args);

#include "ctype.h"

#endif // SH_DEFS_H
