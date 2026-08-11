/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// UNIX DEPENDENT PROCEDURES

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"
#include "defs.h"

// DEFAULT RULES.  Trimmed to the toolchain that is on this image: v7's f77,
// Ratfor and EFL rules and its -ly/-ll flags name nothing that is here.
// README.md, "The default rules".
// clang-format off
char *builtin[] = {
    ".SUFFIXES : .out .o .c .s .y .l",
    "YACC=yacc",
    "YFLAGS=",
    "LEX=lex",
    "LFLAGS=",
    "CC=cc",
    "AS=as",
    "CFLAGS=",
    "LOADLIBES=",

    ".c.o :",
    "\t$(CC) $(CFLAGS) -c $<",

    ".s.o :",
    "\t$(AS) -o $@ $<",

    ".y.o :",
    "\t$(YACC) $(YFLAGS) $<",
    "\t$(CC) $(CFLAGS) -c y.tab.c",
    "\trm y.tab.c",
    "\tmv y.tab.o $@",

    ".l.o :",
    "\t$(LEX) $(LFLAGS) $<",
    "\t$(CC) $(CFLAGS) -c lex.yy.c",
    "\trm lex.yy.c",
    "\tmv lex.yy.o $@",

    ".y.c :",
    "\t$(YACC) $(YFLAGS) $<",
    "\tmv y.tab.c $@",

    ".l.c :",
    "\t$(LEX) $<",
    "\tmv lex.yy.c $@",

    ".s.out .c.out .o.out :",
    "\t$(CC) $(CFLAGS) $< $(LOADLIBES) -o $@",

    ".y.out :",
    "\t$(YACC) $(YFLAGS) $<",
    "\t$(CC) $(CFLAGS) y.tab.c $(LOADLIBES) -o $@",
    "\trm y.tab.c",

    ".l.out :",
    "\t$(LEX) $<",
    "\t$(CC) $(CFLAGS) lex.yy.c $(LOADLIBES) -o $@",
    "\trm lex.yy.c",

    0
};
// clang-format on

static TIMETYPE lookarch(char *filename);
static int amatch(char *s, char *p);
static int umatch(char *s, char *p);

TIMETYPE exists(char *filename)
{
    struct stat buf;
    char *s;

    for (s = filename; *s != '\0' && *s != '('; ++s)
        ;

    if (*s == '(')
        return lookarch(filename);

    if (stat(filename, &buf) < 0)
        return 0;
    else
        return buf.st_mtime;
}

TIMETYPE prestime(void)
{
    time_t t;

    time(&t);
    return t;
}

// Names matching pat, from a directory kept open and rewound.  v7 read raw
// `struct direct' records: DIRSIZ is 18 here and a name off the disk carries no
// terminator, so this goes through opendir(3).  ../README.md SS5.
struct depblock *srchdir(char *pat, int mkchain, struct depblock *nextdbl)
{
    DIR *dirf;
    char *dirname, *dirpref, *endir, *filepat, *p, temp[100];
    char fullname[100];
    struct dirent *dp;
    struct nameblock *q;
    struct depblock *thisdbl;
    struct opendir *od;
    struct pattern *patp;

    thisdbl = 0;

    if (mkchain == NO)
        for (patp = firstpat; patp; patp = patp->nxtpattern)
            if (!unequal(pat, patp->patval))
                return 0;

    patp             = ALLOC(pattern);
    patp->nxtpattern = firstpat;
    firstpat         = patp;
    patp->patval     = copys(pat);

    endir = 0;

    for (p = pat; *p != '\0'; ++p)
        if (*p == '/')
            endir = p;

    if (endir == 0) {
        dirname = ".";
        dirpref = "";
        filepat = pat;
    } else {
        dirname = pat;
        *endir  = '\0';
        if (strlen(dirname) + 2 > sizeof(temp))
            fatal1("Directory name too long: %s", dirname);
        dirpref = concat(dirname, "/", temp);
        filepat = endir + 1;
    }

    dirf = NULL;

    for (od = firstod; od; od = od->nxtopendir)
        if (!unequal(dirname, od->dirn)) {
            dirf = od->dirfc;
            if (dirf != NULL)
                rewinddir(dirf); // start over at the beginning
            break;
        }

    if (od == NULL) {
        dirf           = opendir(dirname);
        od             = ALLOC(opendir);
        od->nxtopendir = firstod;
        firstod        = od;
        od->dirfc      = dirf;
        od->dirn       = copys(dirname);
    }

    if (dirf == NULL) {
        fprintf(stderr, "Directory %s: ", dirname);
        fatal("Cannot open");
    }

    while ((dp = readdir(dirf)) != NULL) {
        if (!amatch(dp->d_name, filepat))
            continue;
        if (strlen(dirpref) + dp->d_namlen + 1 > sizeof(fullname))
            continue;
        concat(dirpref, dp->d_name, fullname);
        if ((q = srchname(fullname)) == 0)
            q = makename(copys(fullname));
        if (mkchain) {
            thisdbl              = ALLOC(depblock);
            thisdbl->nxtdepblock = nextdbl;
            thisdbl->depname     = q;
            nextdbl              = thisdbl;
        }
    }

    if (endir != 0)
        *endir = '/';

    return thisdbl;
}

// stolen from glob through find.  v7 recursed once per matched character;
// iterating instead keeps the depth to the number of `*'s in the pattern.
static int amatch(char *s, char *p)
{
    int cc, scc, k;
    int c, lc;

    for (;;) {
        scc = *s;
        lc  = 077777;
        switch (c = *p) {
        case '[':
            k = 0;
            while ((cc = *++p)) {
                switch (cc) {
                case ']':
                    if (k)
                        goto advance;
                    else
                        return 0;

                case '-':
                    k |= (lc <= scc) & (scc <= (cc = p[1]));
                }
                if (scc == (lc = cc))
                    k++;
            }
            return 0;

        case '?':
            if (scc)
                goto advance;
            return 0;

        case '*':
            return umatch(s, ++p);

        case 0:
            return !scc;
        }

        if (c != scc)
            return 0;
    advance:
        ++s;
        ++p;
    }
}

static int umatch(char *s, char *p)
{
    if (*p == 0)
        return 1;
    while (*s)
        if (amatch(s++, p))
            return 1;
    return 0;
}

// Look inside archives for notations a(b) and a((b))
//      a(b)    is file member  b  in archive a
//      a((b))  is entry point  b  in object archive a
//
// v7 fread() the on-disk structs straight into memory.  Here an archive header,
// an object header and a symbol are six-byte words read through cmd/libaout, and
// a member name and a symbol name are whole strings rather than 14 and 8 bytes.
// The walk is cmd/nm/nm.c's.
static TIMETYPE lookarch(char *filename)
{
    char *p, *q, *qend;
    char member[ARMAXNAME + 1];
    int objarch;
    FILE *fi;
    struct ar_hdr arhdr;
    struct exec hdr;
    struct nlist sym;
    long off, n;
    int c;
    TIMETYPE date;

    for (p = filename; *p != '('; ++p)
        ;
    *p = '\0';
    fi = fopen(filename, "r");
    if (fi == NULL)
        fatal1("cannot open %s", filename);
    if (fgetw(fi) != ARMAG)
        fatal1("%s is not an archive", filename);
    *p++ = '(';

    if (*p == '(') {
        objarch = YES;
        ++p;
    } else
        objarch = NO;

    qend = member + ARMAXNAME;
    for (q = member; q < qend && *p != '\0' && *p != ')'; *q++ = *p++)
        ;
    *q = '\0';

    date          = 0;
    arhdr.ar_name = NULL;
    off           = 6L; // the first member header follows the one-word ARMAG

    for (;;) {
        fseek(fi, off, 0);
        free(arhdr.ar_name);
        arhdr.ar_name = NULL;
        if (!fgetarhdr(fi, &arhdr))
            break;
        off = arhdr.ar_size + ftell(fi);

        if (!objarch) {
            if (!unequal(arhdr.ar_name, member)) {
                date = arhdr.ar_date;
                break;
            }
            continue;
        }

        // An entry point: read the member as an object and scan its symbols.
        if (!fgethdr(fi, &hdr) || N_BADMAG(hdr))
            continue;
        n = hdr.a_const + hdr.a_text + hdr.a_data;
        if (!(hdr.a_flag & RELFLG))
            n *= 2; // the relocation records are still there
        fseek(fi, n, 1);

        for (n = hdr.a_syms; n > 0; n -= c) {
            sym.n_name = NULL;
            c          = fgetsym(fi, &sym);
            if (c == 0)
                fatal("out of memory");
            if (c == 1)
                break;
            if ((sym.n_type & N_EXT) && ((sym.n_type & ~N_EXT) || sym.n_value) &&
                !unequal(sym.n_name, member))
                date = arhdr.ar_date;
            free(sym.n_name);
            if (date)
                break;
        }
        if (date)
            break;
    }

    free(arhdr.ar_name);
    fclose(fi);
    return date;
}
