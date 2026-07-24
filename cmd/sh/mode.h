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

// heap storage
struct blk {
    BLKPTR word;
};

struct fileblk {
    UFD fdes;
    POS flin;
    BOOL feof;
    INT fsiz;
    STRING fnxt;
    STRING fend;
    STRING *feval;
    SHFILE fstak;
    CHAR fbuf[SHBUFSIZ];
};

// for files not used with file descriptors
struct filehdr {
    UFD fdes;
    POS flin;
    BOOL feof;
    INT fsiz;
    STRING fnxt;
    STRING fend;
    STRING *feval;
    SHFILE fstak;
    CHAR _fbuf[1];
};

struct sysnod {
    STRING sysnam;
    INT sysval;
};

// this node is a proforma for those that follow
struct trenod {
    INT tretyp;
    IOPTR treio;
};

// dummy for access only
struct argnod {
    ARGPTR argnxt;
    CHAR argval[1];
};

struct dolnod {
    DOLPTR dolnxt;
    INT doluse;
    CHAR dolarg[1];
};

struct forknod {
    INT forktyp;
    IOPTR forkio;
    TREPTR forktre;
};

struct comnod {
    INT comtyp;
    IOPTR comio;
    ARGPTR comarg;
    ARGPTR comset;
};

struct ifnod {
    INT iftyp;
    TREPTR iftre;
    TREPTR thtre;
    TREPTR eltre;
};

struct whnod {
    INT whtyp;
    TREPTR whtre;
    TREPTR dotre;
};

struct fornod {
    INT fortyp;
    TREPTR fortre;
    STRING fornam;
    COMPTR forlst;
};

struct swnod {
    INT swtyp;
    STRING swarg;
    REGPTR swlst;
};

struct regnod {
    ARGPTR regptr;
    TREPTR regcom;
    REGPTR regnxt;
};

struct parnod {
    INT partyp;
    TREPTR partre;
};

struct lstnod {
    INT lsttyp;
    TREPTR lstlef;
    TREPTR lstrit;
};

struct ionod {
    INT iofile;
    STRING ioname;
    IOPTR ionxt;
    IOPTR iolst;
};

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
