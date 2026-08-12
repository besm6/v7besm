/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// find -- walk a directory hierarchy, testing each file against a boolean expression.
//
//      find pathname-list expression
//
// The last of task C5f's seven (../TODO.md), the largest of them at 725 lines, and THE ONLY
// ONE WITH NO b6sim HALF AT ALL -- the second program in this directory after mount/umount
// to have none.  ./README.md is the account; it reads directory descriptors, which b6sim
// refuses, popen()s pwd, and fork/execvp's for -exec, so every assertion about it is in
// kernel/test/filters and there is no cmd/find/test at all.  Saying that out loud is C4f's
// rule: a deferral written down is the difference between a known gap and an unknown one.
//
// THE PARSE TREE WAS A UNION BY REINTERPRETATION, and that is the port's headline.  v7's
//
//	struct anode { int (*F)(); struct anode *L, *R; } Node[100];
//
// holds, in L and R, whichever of an int, a char or a `char *' the primary wanted -- and
// every primary reads the node back through a private struct shape of its own:
//
//	mk(glob,  (struct anode *)b,       (struct anode *)0)   /* b is a char *  */
//	mk(mtime, (struct anode *)atoi(b), (struct anode *)s)   /* an int and a char */
//	glob(p) register struct { int f; char *pat; } *p;  { return gmatch(Fname, p->pat); }
//
// A `char *' here is a FAT pointer -- bit 48 set, a byte offset in bits 47-45 -- and casting
// it to a `struct anode *' FLOORS it to the word (../README.md §2's third hazard, the one
// the compiler's 2026-06-17 fix does not cover).  So `-name' would have matched against the
// first six bytes of whatever word its pattern started in.  The node carries a `pat', a
// `num' and a `sign' of the right types now, and every primary takes `struct anode *'.
//
// -cpio IS DELETED, decision (B) of the task brief.  It wrote a PDP-11 cpio archive out of
// 16-bit shorts, through a run-time byte-order probe (`union { long l; short s[2]; char
// c[4]; }'), and chgreel() prompted on /dev/tty for the next TAPE REEL.  ../TODO.md's
// exclusion table drops all tape, this kernel has no tape driver and no bdevsw row for one,
// and an archive nothing here can read is not a service.  It took ~140 lines with it,
// including this file's only sbrk, its only `short' and its only /dev/tty -- so `find' now
// calls sbrk NOWHERE, and ../README.md §2's "find and make are the two left" for the three
// arena hazards becomes just `make'.  getty's speed table and col's half-shift are the
// precedent; find.1.umm says so and an unknown primary is diagnosed rather than ignored.
//
// -size IS IN 1024-BYTE BLOCKS, decision (C).  v7's was 512, which names nothing on this
// machine (§4: a constant is the user's business only while it still names something here),
// and the four programs that report a block count -- df, du, quot, ls -s -- were all taught
// KBYTE in task C4a.  A user types the number they read out of `ls -s'.
//
// descend() READS THE DIRECTORY WITH opendir(3), task C24, and that retired the three PDP-11
// layout constants it carried -- a 512-byte read, `dsize>>4' for a 16-byte entry, and a bare
// `for (i = 0; i < 14; ++i)' over the name.  What stays is the BATCH: a bufferful of names is
// taken before any of them recurses, which is what lets the descriptor be dropped and re-taken
// between batches so that a deep tree cannot exhaust NOFILE.  README.md is the account.
//
// AND descend() IS RECURSIVE, WITH ITS BATCH ON THE HEAP.  v7's `struct direct dentry[32]' was
// in the FRAME -- 128 words a level here, so about thirty levels filled the four-page stack §6
// names and nothing checked it, which is C5c's grep(1) finding, where the region past the stack
// returns wrong answers for a dozen levels before it faults.  The depth is COUNTED now, with a
// diagnostic; see MAXDEPTH below for the measurement.
//
// The rest is §1 and §6: `exp' collides with libm's, `ctime' with <time.h>'s, and `index',
// `size', `type', `print', `and', `or', `not' and `pr' were all file-scope; execvp() takes
// two arguments here and v7 gave it three; Pathname[200] and Home[128] were filled by
// unbounded strcpy/fgets/concatenation; and getunum() parsed /etc/passwd by hand into
// char[20] with no bound, where getpwnam(3) and getgrnam(3) already exist.
//
// NOT SETUID: it opens and stats what the caller could open and stat itself, and -exec runs
// as the caller.
//
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define A_DAY   86400 // a day full of seconds
#define EQ(x, y) (strcmp(x, y) == 0)

#define NNODE   100 // parse-tree nodes
#define NENTRY  32  // names per batch -- see descend()
#define PATHSZ  200
#define HOMESZ  128
#define NARGV   50 // -exec argument slots

// v7 rationed descriptors against NOFILE with a bare `10', as ../du does.
#define DIRFDMAX (NOFILE / 2)

// §4: a count reported to -- or taken from -- a user is in 1024-byte blocks, as df, du, quot
// and ls -s all are since task C4a.  KBYTE and KBPB live beside BSIZE in <sys/param.h> so
// that retuning the block size cannot leave this program quietly lying.
_Static_assert(BSIZE % KBYTE == 0, "a filesystem block must be a whole number of KiB");

//
// THE RECURSION CEILING, MEASURED RATHER THAN ESTIMATED, which is C5c's and C5e's shared
// finding and this port's own correction to itself.  descend() calls itself once per
// directory it enters.  v7 held its `struct direct dentry[32]' IN THAT FRAME -- 128 words a
// level here, where a PDP-11 entry was 16 bytes and this one is 24 -- and nothing checked the
// depth at all, so about thirty levels filled the four-page stack §6 names.
//
// The batch is on the HEAP now, one per level, freed on the way out: it is the largest thing
// in the frame by far and moving it is what buys the depth back.  The first draft of this
// port merely shrank it to 16 entries and guessed the frame at eighty words; `b6disasm' says
// that prologue was `15 utm 0277' -- 191 words -- so the guess was out by more than a factor
// of two and MAXDEPTH would have been wrong in the unsafe direction.  ALWAYS READ THE
// PROLOGUE.  (NENTRY is back to v7's 32 now that the batch is not in the frame: 32 names are
// 101 words on the heap and it halves the number of read(2) calls a big directory takes.)
//
// `b6disasm' puts descend()'s prologue at `15 utm 0156' -- 110 words a level, where the raw
// reader's scalars made it 147.  The arithmetic for the limit below:
//
//	20 levels x 110 words                          2,200
//	the deepest thing it can call while stopping      127   (fputs 19 + _flsbuf 108)
//	                                               -------
//	                                                 2,327   of 4,096
//
// -- which leaves main()'s frame and the kernel's own margin well over a third of the stack.
// C5e's warning about the diagnostic's own frame does not bite as hard here as it did in sed,
// and the reason is worth naming: THIS PROGRAM LINKS NO _doprnt AT ALL.  pr() is fputs() and
// -print is puts(); there is not one numeric conversion in it.  §6's rule that what a program
// prints with dominates what it does, seen from the stack rather than the image.  MAXDEPTH
// stays 20 because find.1.umm documents it, not because the stack demands it.
//
// The DIR the batch is read through costs heap too, but the descriptor budget bounds it: at
// most DIRFDMAX of them are open at once however deep the tree goes.  README.md prices it.
//
#define MAXDEPTH 20

static int Randlast;
static char Pathname[PATHSZ];

struct anode {
    int (*F)(struct anode *);
    struct anode *L, *R;
    const char *pat; // -name
    int num;         // the numeric operand, or an argv index for -exec/-ok
    int sign;        // '+', '-', or 0
};

static struct anode Node[NNODE];
static int Nn; // number of nodes
static const char *Fname;
static time_t Now;
static int Argc, Ai, Pi;
static char **Argv;
static char Home[HOMESZ];
static time_t Newer;
static struct stat Statb;
static int Depth;

static struct anode *parse_or(void);
static struct anode *parse_and(void);
static struct anode *parse_not(void);
static struct anode *parse_prim(void);
static struct anode *mk(int (*f)(struct anode *), struct anode *l, struct anode *r);
static struct anode *mknum(int (*f)(struct anode *), int num, int sign);
static struct anode *mkpat(int (*f)(struct anode *), const char *pat);
static char *nxtarg(void);

static int f_and(struct anode *p);
static int f_or(struct anode *p);
static int f_not(struct anode *p);
static int f_name(struct anode *p);
static int f_print(struct anode *p);
static int f_mtime(struct anode *p);
static int f_atime(struct anode *p);
static int f_chgtime(struct anode *p);
static int f_user(struct anode *p);
static int f_ino(struct anode *p);
static int f_group(struct anode *p);
static int f_links(struct anode *p);
static int f_size(struct anode *p);
static int f_perm(struct anode *p);
static int f_type(struct anode *p);
static int f_exec(struct anode *p);
static int f_ok(struct anode *p);
static int f_newer(struct anode *p);

static int scomp(int a, int b, int s);
static int doex(int com);
static int descend(char *name, const char *fname, struct anode *exlist);
static int gmatch(const char *s, const char *p);
static int amatch(const char *s, const char *p);
static int umatch(const char *s, const char *p);
static void pr(const char *s);

int main(int argc, char **argv)
{
    struct anode *exlist;
    int paths;
    char *cp, *sp = NULL;
    FILE *pwd;
    int n;

    time(&Now);
    // The only way this system has to name the working directory: there is no getcwd(3) in
    // v7's libc and none here.  /bin/sh and /bin/pwd are both on the image.
    pwd = popen("pwd", "r");
    if (pwd == NULL || fgets(Home, sizeof(Home), pwd) == NULL) {
        pr("find: cannot find the working directory\n");
        exit(1);
    }
    pclose(pwd);
    n = strlen(Home);
    if (n > 0 && Home[n - 1] == '\n') // v7 wrote Home[strlen(Home)-1] unconditionally
        Home[n - 1] = '\0';
    Argc = argc;
    Argv = argv;
    if (argc < 3) {
    usage:
        pr("Usage: find path-list predicate-list\n");
        exit(1);
    }
    for (Ai = paths = 1; Ai < (argc - 1); ++Ai, ++paths)
        if (*Argv[Ai] == '-' || EQ(Argv[Ai], "(") || EQ(Argv[Ai], "!"))
            break;
    if (paths == 1) // no path-list
        goto usage;
    if (!(exlist = parse_or())) { // parse and compile the arguments
        pr("find: parsing error\n");
        exit(1);
    }
    if (Ai < argc) {
        pr("find: missing conjunction\n");
        exit(1);
    }
    for (Pi = 1; Pi < paths; ++Pi) {
        sp = NULL;
        chdir(Home);
        if (strlen(Argv[Pi]) >= sizeof(Pathname)) {
            pr("find: path too long: "), pr(Argv[Pi]), pr("\n");
            exit(1);
        }
        strcpy(Pathname, Argv[Pi]);
        if ((cp = strrchr(Pathname, '/')) != NULL) {
            sp  = cp + 1;
            *cp = '\0';
            if (chdir(*Pathname ? Pathname : "/") == -1) {
                pr("find: bad starting directory\n");
                exit(2);
            }
            *cp = '/';
        }
        Fname = sp ? sp : Pathname;
        Depth = 0;
        descend(Pathname, Fname, exlist); // to find files that match
    }
    return 0;
}

// compile time functions:  priority is  parse_or() < parse_and() < parse_not() < parse_prim()

// parse ALTERNATION (-o)
static struct anode *parse_or(void)
{
    struct anode *p1;

    p1 = parse_and(); // get left operand
    if (EQ(nxtarg(), "-o")) {
        Randlast--;
        return mk(f_or, p1, parse_or());
    } else if (Ai <= Argc)
        --Ai;
    return p1;
}

// parse CONCATENATION (formerly -a)
static struct anode *parse_and(void)
{
    struct anode *p1;
    char *a;

    p1 = parse_not();
    a  = nxtarg();
    if (EQ(a, "-a")) {
    And:
        Randlast--;
        return mk(f_and, p1, parse_and());
    } else if (EQ(a, "(") || EQ(a, "!") || (*a == '-' && !EQ(a, "-o"))) {
        --Ai;
        goto And;
    } else if (Ai <= Argc)
        --Ai;
    return p1;
}

// parse NOT (!)
static struct anode *parse_not(void)
{
    if (Randlast) {
        pr("find: operand follows operand\n");
        exit(1);
    }
    Randlast++;
    if (EQ(nxtarg(), "!"))
        return mk(f_not, parse_prim(), NULL);
    else if (Ai <= Argc)
        --Ai;
    return parse_prim();
}

// parse parens and predicates
static struct anode *parse_prim(void)
{
    struct anode *p1;
    int i;
    char *a, *b, s;
    struct passwd *pw;
    struct group *gr;

    a = nxtarg();
    if (EQ(a, "(")) {
        Randlast--;
        p1 = parse_or();
        a  = nxtarg();
        if (!EQ(a, ")"))
            goto err;
        return p1;
    } else if (EQ(a, "-print")) {
        return mk(f_print, NULL, NULL);
    }
    b = nxtarg();
    s = *b;
    if (s == '+')
        b++;
    if (EQ(a, "-name"))
        return mkpat(f_name, b);
    else if (EQ(a, "-mtime"))
        return mknum(f_mtime, atoi(b), s);
    else if (EQ(a, "-atime"))
        return mknum(f_atime, atoi(b), s);
    else if (EQ(a, "-ctime"))
        return mknum(f_chgtime, atoi(b), s);
    else if (EQ(a, "-user")) {
        // v7 parsed /etc/passwd by hand into char[20] with no bound; getpwnam(3) exists.
        if ((pw = getpwnam(b)) == NULL) {
            if (gmatch(b, "[0-9][0-9][0-9]*") || gmatch(b, "[0-9][0-9]") ||
                gmatch(b, "[0-9]"))
                return mknum(f_user, atoi(b), s);
            pr("find: cannot find -user name\n");
            exit(1);
        }
        return mknum(f_user, pw->pw_uid, s);
    } else if (EQ(a, "-inum"))
        return mknum(f_ino, atoi(b), s);
    else if (EQ(a, "-group")) {
        if ((gr = getgrnam(b)) == NULL) {
            if (gmatch(b, "[0-9][0-9][0-9]*") || gmatch(b, "[0-9][0-9]") ||
                gmatch(b, "[0-9]"))
                return mknum(f_group, atoi(b), s);
            pr("find: cannot find -group name\n");
            exit(1);
        }
        return mknum(f_group, gr->gr_gid, s);
    } else if (EQ(a, "-size"))
        return mknum(f_size, atoi(b), s);
    else if (EQ(a, "-links"))
        return mknum(f_links, atoi(b), s);
    else if (EQ(a, "-perm")) {
        for (i = 0; *b; ++b) {
            if (*b == '-')
                continue;
            i <<= 3;
            i = i + (*b - '0');
        }
        return mknum(f_perm, i, s);
    } else if (EQ(a, "-type")) {
        i = s == 'd'   ? S_IFDIR
            : s == 'b' ? S_IFBLK
            : s == 'c' ? S_IFCHR
            : s == 'f' ? S_IFREG // v7 wrote the literal 0100000
                       : 0;
        return mknum(f_type, i, 0);
    } else if (EQ(a, "-exec")) {
        i = Ai - 1;
        while (!EQ(nxtarg(), ";"))
            ;
        return mknum(f_exec, i, 0);
    } else if (EQ(a, "-ok")) {
        i = Ai - 1;
        while (!EQ(nxtarg(), ";"))
            ;
        return mknum(f_ok, i, 0);
    } else if (EQ(a, "-newer")) {
        if (stat(b, &Statb) < 0) {
            pr("find: cannot access "), pr(b), pr("\n");
            exit(1);
        }
        Newer = Statb.st_mtime;
        return mk(f_newer, NULL, NULL);
    }
err:
    pr("find: bad option "), pr(a), pr("\n");
    exit(1);
}

static struct anode *mk(int (*f)(struct anode *), struct anode *l, struct anode *r)
{
    if (Nn >= NNODE) {
        pr("find: expression too long\n");
        exit(1);
    }
    Node[Nn].F = f;
    Node[Nn].L = l;
    Node[Nn].R = r;
    return &Node[Nn++];
}

//
// A numeric predicate, and a pattern predicate.  v7 stuffed both through the L and R pointer
// slots and read them back through a private struct shape per primary; see the head of this
// file for why that cannot work with a fat `char *'.
//
static struct anode *mknum(int (*f)(struct anode *), int num, int sign)
{
    struct anode *p = mk(f, NULL, NULL);

    p->num  = num;
    p->sign = sign;
    return p;
}

static struct anode *mkpat(int (*f)(struct anode *), const char *pat)
{
    struct anode *p = mk(f, NULL, NULL);

    p->pat = pat;
    return p;
}

static char *nxtarg(void) // get next arg from command line
{
    static int strikes = 0;

    if (strikes == 3) {
        pr("find: incomplete statement\n");
        exit(1);
    }
    if (Ai >= Argc) {
        strikes++;
        Ai = Argc + 1;
        return "";
    }
    return Argv[Ai++];
}

// execution time functions

static int f_and(struct anode *p)
{
    return ((*p->L->F)(p->L)) && ((*p->R->F)(p->R)) ? 1 : 0;
}

static int f_or(struct anode *p)
{
    return ((*p->L->F)(p->L)) || ((*p->R->F)(p->R)) ? 1 : 0;
}

static int f_not(struct anode *p)
{
    return !((*p->L->F)(p->L));
}

static int f_name(struct anode *p)
{
    return gmatch(Fname, p->pat);
}

static int f_print(struct anode *p)
{
    (void)p;
    puts(Pathname);
    return 1;
}

static int f_mtime(struct anode *p)
{
    return scomp((int)((Now - Statb.st_mtime) / A_DAY), p->num, p->sign);
}

static int f_atime(struct anode *p)
{
    return scomp((int)((Now - Statb.st_atime) / A_DAY), p->num, p->sign);
}

static int f_chgtime(struct anode *p) // v7 called this ctime(), which is <time.h>'s
{
    return scomp((int)((Now - Statb.st_ctime) / A_DAY), p->num, p->sign);
}

static int f_user(struct anode *p)
{
    return scomp(Statb.st_uid, p->num, p->sign);
}

static int f_ino(struct anode *p)
{
    return scomp((int)Statb.st_ino, p->num, p->sign);
}

static int f_group(struct anode *p)
{
    return p->num == Statb.st_gid;
}

static int f_links(struct anode *p)
{
    return scomp(Statb.st_nlink, p->num, p->sign);
}

//
// -size, in 1024-BYTE BLOCKS.  v7's were 512, which names nothing here (§4).  The division
// is at the comparison and nowhere else, so st_size stays in bytes right up to this line.
//
static int f_size(struct anode *p)
{
    return scomp((int)((Statb.st_size + KBYTE - 1) / KBYTE), p->num, p->sign);
}

static int f_perm(struct anode *p)
{
    int i;

    i = (p->sign == '-') ? p->num : 07777; // `-' means only arg bits
    return (Statb.st_mode & i & 07777) == p->num;
}

static int f_type(struct anode *p)
{
    return (Statb.st_mode & S_IFMT) == p->num;
}

static int f_exec(struct anode *p)
{
    fflush(stdout); // to flush possible `-print'
    return doex(p->num);
}

static int f_ok(struct anode *p)
{
    int c;
    int yes;

    yes = 0;
    fflush(stdout); // to flush possible `-print'
    pr("< "), pr(Argv[p->num]), pr(" ... "), pr(Pathname), pr(" >?   ");
    fflush(stderr);
    if ((c = getchar()) == 'y')
        yes = 1;
    while (c != '\n')
        if (c == EOF)
            exit(2);
        else
            c = getchar();
    if (yes)
        return doex(p->num);
    return 0;
}

static int f_newer(struct anode *p)
{
    (void)p;
    return Statb.st_mtime > Newer;
}

// support functions

static int scomp(int a, int b, int s) // funny signed compare
{
    if (s == '+')
        return a > b;
    if (s == '-')
        return a < (b * -1);
    return a == b;
}

static int doex(int com)
{
    int np;
    char *na;
    static char *nargv[NARGV];
    static int ccode;

    ccode = np = 0;
    while ((na = Argv[com++]) != NULL) {
        if (strcmp(na, ";") == 0)
            break;
        if (np >= NARGV - 1) { // v7 had no bound here at all
            pr("find: -exec command too long\n");
            exit(1);
        }
        if (strcmp(na, "{}") == 0)
            nargv[np++] = Pathname;
        else
            nargv[np++] = na;
    }
    nargv[np] = NULL;
    if (np == 0)
        return 9;
    if (fork()) // parent
        wait(&ccode);
    else { // child
        chdir(Home);
        execvp(nargv[0], nargv); // v7 passed a third argument
        exit(1);
    }
    return ccode ? 0 : 1;
}

//
// Walk `fname' (relative to the current directory) as `name' (the path from the top), and
// apply the compiled expression to it and to everything under it.
//
static int descend(char *name, const char *fname, struct anode *exlist)
{
    DIR *dirp = NULL; // NULL meaning `dropped', not `end of directory'
    struct dirent *dp;
    char (*names)[DIRSIZ + 1]; // one batch, read before any of it recurses
    long loc;
    int nb, k, done = 0;
    char *c1;
    const char *c2;
    int i;
    int rv = 0;
    char *endofname;

    if (stat(fname, &Statb) < 0) {
        pr("find: bad status-- "), pr(name), pr("\n");
        return 0;
    }
    (*exlist->F)(exlist);
    if ((Statb.st_mode & S_IFMT) != S_IFDIR)
        return 1;

    // §6's third ceiling, which nothing else checks: see MAXDEPTH.
    if (++Depth > MAXDEPTH) {
        --Depth;
        pr("find: directory tree too deep: "), pr(name), pr("\n");
        return 1;
    }
    // On the heap and not in the frame: 32 names are 101 words, and descend() recurses.
    names = malloc(NENTRY * (DIRSIZ + 1));
    if (names == NULL) {
        --Depth;
        pr("find: out of memory\n");
        exit(1);
    }

    for (c1 = name; *c1; ++c1)
        ;
    if (*(c1 - 1) == '/')
        --c1;
    endofname = c1;

    if (chdir(fname) == -1) {
        free((char *)names);
        --Depth;
        return 0;
    }
    if ((dirp = opendir(".")) == NULL) {
        pr("find: cannot open "), pr(name), pr("\n");
        rv = 0;
        goto ret;
    }

    while (!done) {
        // One batch of names, taken before any of them recurses: that is what makes the
        // descriptor droppable below, and it is what the raw reader's bufferful was for.
        for (nb = 0; nb < NENTRY;) {
            if ((dp = readdir(dirp)) == NULL) {
                done = 1;
                break;
            }
            // v7 abandoned the whole directory on an empty name, with a `??' beside it.
            // A corrupt entry is skipped instead.
            if (dp->d_namlen == 0 || EQ(dp->d_name, ".") || EQ(dp->d_name, ".."))
                continue;
            strcpy(names[nb++], dp->d_name);
        }

        // A descriptor budget: above DIRFDMAX the directory is dropped and re-taken per
        // batch, so a deep tree cannot exhaust NOFILE.  The cookie survives the closedir(),
        // and descend() leaves `.' where it found it.
        loc = telldir(dirp);
        if (dirfd(dirp) > DIRFDMAX) {
            closedir(dirp);
            dirp = NULL;
        }

        for (k = 0; k < nb; ++k) {
            if (endofname + 1 + DIRSIZ + 1 > Pathname + sizeof(Pathname)) {
                pr("find: path too long: "), pr(name), pr("\n");
                continue;
            }
            c1    = endofname;
            *c1++ = '/';
            c2    = names[k];
            while (*c2)
                *c1++ = *c2++;
            *c1 = '\0';

            Fname = endofname + 1;
            if (!descend(name, Fname, exlist)) {
                *endofname = '\0';
                chdir(Home);
                if (chdir(Pathname) == -1) {
                    pr("find: bad directory tree\n");
                    exit(1);
                }
            }
        }

        if (!done && dirp == NULL) {
            if ((dirp = opendir(".")) == NULL) {
                pr("find: cannot open "), pr(name), pr("\n");
                rv = 0;
                goto ret;
            }
            seekdir(dirp, loc);
        }
    }
    rv = 1;
ret:
    if (dirp)
        closedir(dirp);
    free((char *)names);
    if (chdir("..") == -1) {
        *endofname = '\0';
        pr("find: bad directory "), pr(name), pr("\n");
        rv = 1;
    }
    --Depth;
    return rv;
}

static int gmatch(const char *s, const char *p) // string match as in glob
{
    if (*s == '.' && *p != '.')
        return 0;
    return amatch(s, p);
}

static int amatch(const char *s, const char *p)
{
    int cc;
    int scc, k;
    int c, lc;

    scc = (unsigned char)*s;
    lc  = 077777;
    switch (c = (unsigned char)*p) {

    case '[':
        k = 0;
        while ((cc = (unsigned char)*++p) != 0) {
            switch (cc) {

            case ']':
                if (k)
                    return amatch(++s, ++p);
                else
                    return 0;

            case '-':
                // v7 wrote a bitwise `&' where `&&' was meant.  It works -- both operands
                // are 0 or 1 -- and it is left as it stands, with this line to say so.
                k |= lc <= scc && scc <= (cc = (unsigned char)p[1]);
            }
            if (scc == (lc = cc))
                k++;
        }
        return 0;

    case '?':
    caseq:
        if (scc)
            return amatch(++s, ++p);
        return 0;
    case '*':
        return umatch(s, ++p);
    case 0:
        return !scc;
    }
    if (c == scc)
        goto caseq;
    return 0;
}

static int umatch(const char *s, const char *p)
{
    if (*p == 0)
        return 1;
    while (*s)
        if (amatch(s++, p))
            return 1;
    return 0;
}

static void pr(const char *s)
{
    fputs(s, stderr);
}
