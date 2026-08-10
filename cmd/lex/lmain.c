//
// lex [-tvfn] [file] ...
//
// Copyright 1976, Bell Telephone Laboratories, Inc.,
// written by Eric Schmidt, August 27, 1976
//
#include "ldefs.h"
#include "once.h"
#include <unistd.h>

static void get1core(void);
static void get2core(void);
static void get3core(void);
static void free1core(void);
static void free2core(void);

// $B6LEXFORM first -- deliberately not keyed on `besm6', as in cc.c -- then the
// profile's search path.  Returns the last candidate if none exists, so the
// caller's diagnostic names a path.  README.md, "Finding the skeleton".
static const char *find_form(void)
{
    static char path[1024];
    const char *override = getenv("B6LEXFORM");

    if (override != NULL && *override != '\0') {
        snprintf(path, sizeof(path), "%s", override);
        return path;
    }

#ifdef besm6
    snprintf(path, sizeof(path), "/usr/lib/lex/ncform");
#else
    const char *home = getenv("HOME");

    if (home != NULL && *home != '\0') {
        snprintf(path, sizeof(path), "%s/.local/share/besm6/lex/ncform", home);
        if (access(path, R_OK) == 0)
            return path;
    }
    snprintf(path, sizeof(path), "/usr/local/share/besm6/lex/ncform");
#endif
    return path;
}

// ldefs.h's LEXBUFSIZ has the arithmetic; on the host the default is affordable.
// A failure leaves the stream unbuffered -- slow but correct -- so it is not fatal.
void shrink_buffer(FILE *f)
{
#if besm6
    setvbuf(f, NULL, _IOFBF, LEXBUFSIZ);
#else
    (void)f;
#endif
}

int main(int argc, char **argv)
{
    register int i;
    const char *form;

    // errorf cannot be initialised where it is defined; nothing may report
    // before this line.
    errorf = stdout;

    while (argc > 1 && argv[1][0] == '-') {
        i = 0;
        while (argv[1][++i]) {
            switch (argv[1][i]) {
            case 't':
            case 'T':
                fout   = stdout;
                errorf = stderr;
                break;
            case 'v':
            case 'V':
                report = 1;
                break;
            case 'f':
            case 'F':
                optim = FALSE;
                break;
            case 'n':
            case 'N':
                report = 0;
                break;
            default:
                warning("Unknown option %c", argv[1][i]);
            }
        }
        argc--;
        argv++;
    }
    sargc = argc;
    sargv = argv;
    if (argc > 1) {
        fin = fopen(argv[++fptr], "r"); // open argv[1]
        sargc--;
    } else
        fin = stdin;
    if (fin == NULL)
        error("Can't read input file %s", argc > 1 ? argv[1] : "standard input");
    shrink_buffer(fin);
    gch();
    // may be gotten: def, subs, sname, schar, ccl, dchar
    get1core();
    // may be gotten: name, left, right, nullstr, parent
    strcpy(sp, "INITIAL");
    sname[0] = sp;
    sp += strlen("INITIAL") + 1;
    sname[1] = 0;
    if (yyparse())
        exit(1); // error return code
    // may be disposed of: def, subs, dchar
    free1core();
    // may be gotten: tmpstat, foll, positions, gotof, nexts, nchar, state,
    // atable, sfall, cpackflg
    get2core();
    ptail();
    mkmatch();
    sect = ENDSECTION;
    if (tptr > 0)
        cfoll(tptr - 1);
    cgoto();
    // may be disposed of: positions, tmpstat, foll, state, name, left, right,
    // parent, ccl, schar, sname
    // may be gotten: verify, advance, stoff
    free2core();
    get3core();
    layout();

    form   = find_form();
    fother = fopen(form, "r");
    if (fother == NULL)
        error("Lex driver missing, file %s", form);
    shrink_buffer(fother);
    while ((i = getc(fother)) != EOF)
        putc(i, fout);

    fclose(fother);
    fclose(fout);
    if (report == 1)
        statistics();
    fclose(stdout);
    fclose(stderr);
    exit(0); // success return code
}

static void get1core(void)
{
    ccptr = ccl = (unsigned char *)myalloc(CCLSIZE, sizeof(*ccl));
    pcptr = pchar = (unsigned char *)myalloc(pchlen, sizeof(*pchar));
    def   = (char **)myalloc(DEFSIZE, sizeof(*def));
    subs  = (char **)myalloc(DEFSIZE, sizeof(*subs));
    dp = dchar = myalloc(DEFCHAR, sizeof(*dchar));
    sname      = (char **)myalloc(STARTSIZE, sizeof(*sname));
    sp = schar = myalloc(STARTCHAR, sizeof(*schar));
}

static void free1core(void)
{
    free(def);
    free(subs);
    free(dchar);
}

static void get2core(void)
{
    register int i;

    gotof    = (int *)myalloc(nstates, sizeof(*gotof));
    nexts    = (int *)myalloc(ntrans, sizeof(*nexts));
    nchar    = (unsigned char *)myalloc(ntrans, sizeof(*nchar));
    state    = (int **)myalloc(nstates, sizeof(*state));
    atable   = (int *)myalloc(nstates, sizeof(*atable));
    sfall    = (int *)myalloc(nstates, sizeof(*sfall));
    cpackflg = myalloc(nstates, sizeof(*cpackflg));
    tmpstat  = myalloc(tptr + 1, sizeof(*tmpstat));
    foll     = (int **)myalloc(tptr + 1, sizeof(*foll));
    nxtpos = positions = (int *)myalloc(maxpos, sizeof(*positions));
    for (i = 0; i <= tptr; i++)
        foll[i] = 0;
}

static void free2core(void)
{
    free(positions);
    free(tmpstat);
    free(foll);
    free(name);
    free(left);
    free(right);
    free(parent);
    free(nullstr);
    free(state);
    free(sname);
    free(schar);
    free(ccl);
}

static void get3core(void)
{
    verify  = (int *)myalloc(outsize, sizeof(*verify));
    advance = (int *)myalloc(outsize, sizeof(*advance));
    stoff   = (int *)myalloc(stnum + 2, sizeof(*stoff));
}

// Fatal on failure: every caller treated a 0 that way, so this reports it once
// instead of nine times.  v7 also tested the result against (char *)-1, a
// fabricated pointer that can never equal a real one (../README.md SS2).
char *myalloc(int a, int b)
{
    char *p;

    p = calloc(a, b);
    if (p == NULL)
        error("Out of memory (%d items of %d bytes)", a, b);
    return (p);
}

// char *, not const char *: this is the signature b6yacc emits into every
// y.tab.c (../yacc/README.md, "The contract").
// cppcheck-suppress constParameterPointer
void yyerror(char *s)
{
    fprintf(stderr, "\"%s\", line %d: %s\n", fptr > 0 ? sargv[fptr] : "<stdin>", yyline, s);
}
