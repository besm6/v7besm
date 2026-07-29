/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The shell's types and its parse-tree nodes.
//
// v7 built this file out of mac.h's TYPE/STRUCT/UNION macros; the macros are gone (see
// defs.h) but the type NAMES are not, because every declaration in the shell is written
// in them and renaming them all is a different change from porting the code.
//
#ifndef SH_MODE_H
#define SH_MODE_H

#include <sys/param.h>

// Char-units per word: 6 on this machine, and the shell's alignment unit.  v7 wrote
// sizeof(char *), which is still 6 here, but NBPW says what is actually meant.
#define BYTESPERWORD NBPW

typedef char CHAR;

//
// BOOL MUST STAY AN INTEGER TYPE -- not <stdbool.h>'s bool.  The name says boolean but
// three of the objects declared with it carry multi-bit flags: trapnote and trapflg[]
// hold TRAPSET|SIGSET|SIGMOD (defs.h), and exfile()'s `prof' parameter is called as
// exfile(ttyflg) and then OR-ed straight into `flags' (main.c).  Under bool that last
// one becomes 1 == noexec, and the shell would parse everything and execute nothing
// with no diagnostic at all.
//
typedef char BOOL;

typedef int UFD;
typedef int INT;
typedef char *ADDRESS;
typedef long int L_INT;
typedef unsigned POS;

//
// v7's `VOID' typedef is gone: it stood for `int', not for void, and the shell has one
// function -- cmd.c's skipnl() -- that is declared VOID and whose RETURNED VALUE decides
// whether `for x in a b c' parses as a list or as a bare `for x'.  Spelling VOID as void
// would have broken that silently, so the typedef is not worth keeping for the other
// thirty sites.  They say `void'; skipnl() says INT.
//
typedef char *STRING;
typedef char MSG[];
typedef int PIPE[];
typedef char *STKPTR;
typedef char *BYTPTR;

typedef struct stat STATBUF; // defined in <sys/stat.h>
typedef struct blk *BLKPTR;

//
// SHFILE, not FILE, and SHBUFSIZ, not BUFSIZ.  v7 had no <stdio.h> to collide with;
// this tree does, and both collisions are silent rather than loud if the shell ever
// reaches that header.  BUFSIZ there is 3072, which truncated into the byte-wide fsiz
// v7 used would be 0, and readc() would see EOF at once and exit before printing a
// prompt.  fsiz is an INT here for the same reason.
//
#define SHBUFSIZ 64
typedef struct fileblk SHFILEBLK;
typedef struct filehdr SHFILEHDR;
typedef struct fileblk *SHFILE;

typedef struct trenod *TREPTR;
typedef struct forknod *FORKPTR;
typedef struct comnod *COMPTR;
typedef struct swnod *SWPTR;
typedef struct regnod *REGPTR;
typedef struct parnod *PARPTR;
typedef struct ifnod *IFPTR;
typedef struct whnod *WHPTR;
typedef struct fornod *FORPTR;
typedef struct lstnod *LSTPTR;
typedef struct argnod *ARGPTR;
typedef struct dolnod *DOLPTR;
typedef struct ionod *IOPTR;
typedef struct namnod NAMNOD;
typedef struct namnod *NAMPTR;
typedef struct sysnod SYSNOD;
typedef struct sysnod *SYSPTR;

//
// v7 had a third one here, `typedef struct sysnod SYSTAB[]', and used it to declare the
// two keyword tables in msg.c.  C11 forbids it: struct sysnod is not defined until
// further down this file, and an ARRAY of an incomplete type is a constraint violation
// even where a pointer to one is fine (6.7.6.2).  The tables are SYSNOD[] instead.
//

#define NIL ((char *)0)

//
// v7's Lcheat/Rcheat -- (*(int *)&a) and ((int)a) -- are gone.  They existed to pun a
// pointer into an integer so a flag could be packed into bit 0 of it, and on this
// machine bit 0 of a word address names the NEXT WORD.  The flags moved to bit 16
// instead; each of the two places that wants one now says so locally (blok.c's BUSY,
// service.c's ARGMK), the way lib/libc/gen/malloc.c does.
//

//
// A block of the heap.  One word, and it is the whole header: it points at the NEXT
// block, so a block's size is the distance to the one after it, and the chain runs in
// address order.  The high bit of the pointer says whether the block is in use --
// bit 16 here, not bit 0; blok.c explains why.
//
struct blk {
    BLKPTR word;
};

//
// One level of the input stack: a place the shell is reading commands from.  `.',
// `eval', a here-document and command substitution each push one of these and pop it
// again, so that reading can be interrupted and resumed.
//
struct fileblk {
    UFD fdes;            // the descriptor, or -1 if reading from a string
    POS flin;            // which line we are on, for the syntax error report
    BOOL feof;           // nothing left to read here
    BOOL fencd;          // this input is ENCODED text -- see below
    INT fsiz;            // how much to read at a time: a bufferful, or one character
                         // when input is a terminal, so that no typing is swallowed
    STRING fnxt;         // the next character of the buffer to hand out
    STRING fend;         // one past the last valid character in the buffer
    STRING *feval;       // `eval' only: the remaining words to read after this string
    SHFILE fstak;        // the input this one interrupted, to go back to
    CHAR fbuf[SHBUFSIZ]; // the buffer itself
};

//
// fencd IS NOT v7's, and it is the second of defs.h's three invariants made mechanical.
// Script text is raw: a 0377 in it is a byte of somebody's data.  Two inputs are not --
// the word macro() pushes and re-reads through the lexer, and the here-document temp
// file, which copy() writes in the stored form so that a backslash in an unquoted
// here-document still means something when subst() reads it back.  readc() decodes QESC
// only when this flag is set, and it is set by exactly those two places.
//

//
// The same thing WITHOUT the buffer, for reading from a string rather than a file.
// Every field lines up with struct fileblk above, and macro() allocates one of these on
// the C stack to save the sixty-odd bytes the buffer would cost on every substitution.
//
struct filehdr {
    UFD fdes;
    POS flin;
    BOOL feof;
    BOOL fencd;
    INT fsiz;
    STRING fnxt;
    STRING fend;
    STRING *feval;
    SHFILE fstak;
    CHAR _fbuf[1];
};

//
// One row of a keyword table: a word, and the number the shell knows it by.  msg.c holds
// the two tables -- `reserved' for if/then/for/..., `commands' for cd/set/export/...
//
struct sysnod {
    STRING sysnam; // the word as the user types it
    INT sysval;    // its symbol number (sym.h) or built-in number (defs.h)
};

//
// ======== the parse tree ========
//
// cmd.c builds one of these nodes per piece of syntax and execute() walks them.  Every
// node begins with a type word and a redirection list, which is what lets the walker
// look at any node before it knows which kind it is; the type word's low bits are the
// kind (TCOM, TFOR, ...) and the rest are flags.
//

// The shape every node starts with.  A node is always reached through one of these
// first, then cast to whichever of the specific types the type word named.
struct trenod {
    INT tretyp;  // what kind of node this is, plus flags
    IOPTR treio; // the < and > redirections attached to it
};

//
// One word of a command line, on a chain of them.  The text follows the link word in the
// same block, so a word costs one allocation rather than two -- argval[1] is a
// stand-in for however many characters there turn out to be.
//
struct argnod {
    ARGPTR argnxt;  // the next word
    CHAR argval[1]; // the text, NUL-terminated
};

//
// A saved list of positional parameters ($1, $2, ...).  Reference-counted, because a
// `for' loop walking the list must keep it alive even if the body runs `set'.
//
struct dolnod {
    DOLPTR dolnxt;  // the next saved list
    INT doluse;     // how many users it still has
    CHAR dolarg[1]; // the vector of pointers itself, following in the same block
};

// "run this in a child process."  What & and | and ( ) are made of.
struct forknod {
    INT forktyp;
    IOPTR forkio;
    TREPTR forktre; // what to run
};

// A simple command: a program name, its arguments, and any variables set just for it.
struct comnod {
    INT comtyp;
    IOPTR comio;
    ARGPTR comarg; // the words: command name first
    ARGPTR comset; // the leading `name=value' assignments, as in `TZ=UTC date'
};

// if ... then ... else ... fi
struct ifnod {
    INT iftyp;
    TREPTR iftre; // the condition
    TREPTR thtre; // what to run if it succeeded
    TREPTR eltre; // what to run if it did not; null if there was no else
};

// while ... do ... done, and until ... do ... done.  Which one is in the type word.
struct whnod {
    INT whtyp;
    TREPTR whtre; // the condition
    TREPTR dotre; // the body
};

// for NAME in WORDS do ... done
struct fornod {
    INT fortyp;
    TREPTR fortre; // the body
    STRING fornam; // the variable to set on each pass
    COMPTR forlst; // the words to walk; null means "walk the positional parameters"
};

// case WORD in ... esac
struct swnod {
    INT swtyp;
    STRING swarg; // the word being matched
    REGPTR swlst; // the arms, in order
};

// One arm of a `case': its patterns, and what to run when one of them matches.
struct regnod {
    ARGPTR regptr; // the patterns, as in `a|b|c)'
    TREPTR regcom; // what to run
    REGPTR regnxt; // the next arm
};

// ( ... ) -- a group run in a child, so that a cd or an assignment inside it is
// forgotten afterwards.
struct parnod {
    INT partyp;
    TREPTR partre;
};

// Two commands joined by ; or && or || or |.  Which one is in the type word.
struct lstnod {
    INT lsttyp;
    TREPTR lstlef; // the left-hand command
    TREPTR lstrit; // the right-hand command
};

// One redirection: `< file', `> file', `2>&1', `<<WORD'.
struct ionod {
    INT iofile;    // which descriptor, plus flag bits saying which kind (IOPUT, IOAPP,
                   // IOMOV, IODOC ...)
    STRING ioname; // the file name, or the here-document's terminator word
    IOPTR ionxt;   // the next redirection on the same command
    IOPTR iolst;   // here-documents only: the chain of temp files to clean up
};

// How big each kind of node is, for getstak().
#define FORKTYPE (sizeof(struct forknod))
#define COMTYPE  (sizeof(struct comnod))
#define IFTYPE   (sizeof(struct ifnod))
#define WHTYPE   (sizeof(struct whnod))
#define FORTYPE  (sizeof(struct fornod))
#define SWTYPE   (sizeof(struct swnod))
#define REGTYPE  (sizeof(struct regnod))
#define PARTYPE  (sizeof(struct parnod))
#define LSTTYPE  (sizeof(struct lstnod))
#define IOTYPE   (sizeof(struct ionod))

#endif // SH_MODE_H
