/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  Redistribution and use in source and binary forms
 * are permitted provided that this notice is preserved and that due credit
 * is given to the University of California at Berkeley.
 */

//
// man -- find a manual page and show it.
//
//      man [ - ] [ -a ] [ -w ] [ -M path ] [ section ] title ...
//
// Task C25b (../TODO.md).  The source is RetroBSD's src/cmd/man/man.c, which is 4.3BSD's;
// v7 HAD a man, but it was a shell script around nroff and there is no nroff here.
//
// THIS PROGRAM DOES NOT FORMAT, and that is the whole design rather than a shortfall.  The
// pages on this image are the dialect ../../doc/Manual_Page_Format.md describes and are
// legible as they stand, so man runs `cat' where every other Unix runs a formatter, and
// pipes into more(1) when standard output is a terminal.  FORMATTER below is the one line
// C25a's renderer replaces; nothing else in this file knows what formatting is.
//
// THE SECTION SEARCH IS AN ORDERED TABLE AND NOT A readdir(), for three reasons in this
// order.  THIRTEEN page names live in two sections at once -- acct, chmod, chown, crypt,
// intro, kill, mount, nice, passwd, sleep, sync, time, write -- and the table's order is the
// only thing that decides which one a bare `man mount' shows; a directory walk would answer
// with whatever order the directory happens to hold.  b6sim cannot read a directory, so a
// readdir design could not be tested in the cheap world (test/CMakeLists.txt).  And a hit on
// `man ls' is one access(2) against opening five directories and reading them.
//
// WHAT THE TABLE DOES NOT SURVIVE is a new subsection LETTER: a page named foo.2v is staged
// onto the image by B6_STAGE_MAN and cannot be found here until "2v" joins the list.  A new
// section DIGIT is already covered -- 4, 6 and 7 are in the table although those directories
// do not exist today, exactly so that the first page written for one is findable.
//
// FIVE THINGS ARE GONE from the 4.3BSD source and none of them is a simplification for its
// own sake: the uname()/$MACHINE machine subdirectories (no <sys/utsname.h> here), the
// local/new/old sections and their paths (no <paths.h> either), the cat1/cat8/cat3f tables
// and the .0 suffix they carried (our pages are sources, in man<digit>/ with the subsection
// letter left on the file), the undocumented -f and -k that exec whatis(1) and apropos(1)
// (neither program nor a `whatis' database is on this image), and getopt(3), which
// this libc has not got (../ls/ls.c's parser is the model).  The MANDIR table went with them
// for a reason of this machine's: a string literal cannot initialise a char * inside a
// struct initializer at all (../../CLAUDE.md), so the sections are a flat array of char *.
//
// FOUR UPSTREAM DEFECTS ARE FIXED RATHER THAN CARRIED; README.md has the account.
//
// NOT SETUID: it opens what its caller could open itself.
//
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXPATH   256        // a page path, terminator included
#define CMDCHUNK  256        // the command string grows by this much at a time
#define FORMATTER "/bin/cat" // THE ONE LINE C25a's renderer replaces
#define DEFPAGER  "/bin/more"
#define DEFPATH   "/usr/man"

//
// The sections, in search order: commands first, as v7 orders the manual itself.  The
// directory is derived -- man + the leading digit -- so this is the only list of them.
//
static const char *const section[] = {
    "1", "1m", "8", "6", "2", "3", "3s", "3m", "3x", "4", "5", "7", 0,
};

static int aflg;      // -a: every page that matches, not the first
static int wflg;      // -w: print where it is, show nothing
static int rawflg;    // -: the page source, unformatted and unpaged
static int notfound;  // the exit status, accumulated over the whole title list
static char *manpath; // -M, or $MANPATH, or DEFPATH
static char *pager;   // $PAGER, or DEFPAGER

static char fname[MAXPATH]; // the path probe() built, and the one found() consumes
static char elem[MAXPATH];  // one element of the colon-separated search path
static char iobuf[BUFSIZ];  // 512 words, and static because the stack has 4096

static char *cmd; // the display command: FORMATTER, the pages, maybe | pager
static int cmdlen;
static int cmdmax;

static void usage(void)
{
    fputs("usage: man [-] [-a] [-w] [-M path] [section] title ...\n", stderr);
    exit(1);
}

static void nospace(void)
{
    fputs("man: out of space\n", stderr);
    exit(1);
}

//
// Append one word to the display command, starting it with the formatter.  v7's add() kept
// a char * cursor and grew the buffer by one realloc that was not a loop; an index is a
// register test where a pointer is a b$pdiff call (../README.md §2), and the loop is a loop.
//
static void addcmd(const char *s)
{
    int need;

    if (cmd == 0) {
        cmdmax = CMDCHUNK;
        cmd    = malloc(cmdmax);
        if (cmd == 0)
            nospace();
        strcpy(cmd, FORMATTER);
        cmdlen = strlen(cmd);
    }
    need = cmdlen + strlen(s) + 2; // the space, and the terminator
    while (need > cmdmax) {
        cmdmax += CMDCHUNK;
        cmd = realloc(cmd, cmdmax);
        if (cmd == 0)
            nospace();
    }
    cmd[cmdlen++] = ' ';
    strcpy(cmd + cmdlen, s);
    cmdlen += strlen(s);
}

//
// The `-' path: the page source, byte for byte.  Upstream wrote `if (!(fd = open(...)))',
// testing the descriptor against 0 -- so a page opened on descriptor 0 was reported as a
// failure and a failed open sailed on into read(2).
//
static void copyout(const char *path)
{
    int fd, n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fflush(stdout);
        perror(path);
        notfound = 1;
        return;
    }
    fflush(stdout); // this writes past stdio, so nothing may be left in it
    while ((n = read(fd, iobuf, sizeof(iobuf))) > 0)
        if (write(1, iobuf, n) != n) {
            perror("man: write");
            exit(1);
        }
    if (n < 0) {
        perror("man: read");
        exit(1);
    }
    close(fd);
}

// What to do with a page that exists.  -w wins over -, and - over the display command.
static void found(const char *path)
{
    if (wflg)
        puts(path);
    else if (rawflg)
        copyout(path);
    else
        addcmd(path);
}

//
// An argument is a section specifier if it begins with a digit.  <ctype.h> is not included
// here and isdigit() is not called: that table has 129 entries (../README.md §11) and a
// title may be any byte at all.
//
static int issect(const char *s)
{
    return s[0] >= '0' && s[0] <= '9';
}

//
// Does a table entry answer the section the caller asked for?  No section matches every
// entry; a bare digit matches the whole family, so `man 3 stdio' finds stdio.3s; anything
// longer must match exactly, so `man 3s' does not reach 3m.
//
static int matches(const char *ent, const char *sect)
{
    if (sect == 0)
        return 1;
    if (sect[1] == '\0')
        return ent[0] == sect[0];
    return strcmp(ent, sect) == 0;
}

static int known(const char *sect)
{
    int i;

    for (i = 0; section[i]; i++)
        if (matches(section[i], sect))
            return 1;
    return 0;
}

//
// Is ${dir}/man${digit}/${name}.${ent} readable?  The path is left in fname.  v7 built it
// with an unbounded sprintf; snprintf returns the length it wanted (C11 §7.21.6.5), so a
// path that does not fit is a miss rather than a wild write.
//
static int probe(const char *dir, const char *ent, const char *name)
{
    if (snprintf(fname, sizeof(fname), "%s/man%c/%s.%s", dir, ent[0], name, ent) >=
        (int)sizeof(fname))
        return 0;
    return access(fname, R_OK) == 0;
}

//
// Walk the search path, and within each element the section table.  PATH OUTER, SECTION
// INNER: a whole -M element is exhausted before the next one is looked at, which is what
// makes a personal manual directory shadow the system one.  The element is copied out
// rather than terminated in place -- $MANPATH's storage is not ours to write on.
//
static int search(const char *sect, const char *name)
{
    const char *beg, *end;
    int i, len, res;

    res = 0;
    for (beg = manpath;; beg = end + 1) {
        end = strchr(beg, ':');
        len = end ? (int)(end - beg) : (int)strlen(beg);
        if (len > 0 && len < (int)sizeof(elem)) {
            memcpy(elem, beg, len);
            elem[len] = '\0';
            for (i = 0; section[i]; i++)
                if (matches(section[i], sect) && probe(elem, section[i], name)) {
                    found(fname);
                    if (!aflg)
                        return 1;
                    res = 1;
                }
        }
        if (end == 0)
            return res;
    }
}

//
// The title list.  A section applies to the title that follows it and to no other, and an
// argument is only a section if a title follows -- `man 3' is a request for a page called 3
// under upstream's rules, and no page name on this image begins with a digit.
//
// Upstream exit(1)ed on the first title it could not find, throwing away the pages it had
// already queued: `man ls nosuch' showed nothing at all.  A miss is recorded and the list
// goes on.
//
static void dotitles(int argc, char **argv, int i)
{
    const char *sect;

    while (i < argc) {
        sect = 0;
        if (issect(argv[i]) && i + 1 < argc) {
            sect = argv[i++];
            if (!known(sect)) {
                fflush(stdout); // stderr is unbuffered: the two must interleave as written
                fprintf(stderr, "man: unknown section %s\n", sect);
                notfound = 1;
                i++; // the title it governed goes with it
                continue;
            }
        }
        if (!search(sect, argv[i])) {
            // Upstream printed nothing here under -w and exited 0: the flag set WHERE|ALL
            // and the miss was tested against ALL.
            fflush(stdout); // ... and a -w hit before it must reach the file first
            fprintf(stderr, "man: no entry for %s in ", argv[i]);
            if (sect)
                fprintf(stderr, "section %s of ", sect);
            fputs("the manual\n", stderr);
            notfound = 1;
        }
        i++;
    }
}

//
// The option parser, in place of getopt(3).  Clustered and separate forms alike -- `man -aw'
// and `man -a -w' -- stopping at the first argument that is not an option or at `--'.  A
// lone `-' is an OPTION here and not a file name, which is the one place this differs from
// ../ls/ls.c's parser.
//
static int options(int argc, char **argv)
{
    int i, ch;
    char *p;

    for (i = 1; i < argc; i++) {
        p = argv[i];
        if (p[0] != '-')
            break;
        if (p[1] == '\0') {
            rawflg++;
            continue;
        }
        if (p[1] == '-' && p[2] == '\0') {
            i++;
            break;
        }
        for (;;) {
            ch = *++p;
            if (ch == '\0')
                break;
            if (ch == 'a') {
                aflg++;
            } else if (ch == 'w') {
                wflg++;
            } else if (ch == 'M') {
                // The rest of the cluster if there is one, else the next argument.
                if (p[1] != '\0')
                    manpath = p + 1;
                else if (++i < argc)
                    manpath = argv[i];
                else
                    usage();
                break;
            } else {
                usage();
            }
        }
    }
    return i;
}

int main(int argc, char **argv)
{
    int i;

    i = options(argc, argv);
    if (i >= argc)
        usage();
    if (manpath == 0)
        manpath = getenv("MANPATH");
    if (manpath == 0 || *manpath == '\0')
        manpath = DEFPATH;
    pager = getenv("PAGER");
    if (pager == 0 || *pager == '\0')
        pager = DEFPAGER;

    dotitles(argc, argv, i);

    //
    // One command for every page found, run through the shell -- which is what lets the
    // pager be a pipeline of the caller's own.  The pager is appended only for a terminal:
    // `man ls | grep foo' should not spend a third process on a pager that has nothing to
    // page to.
    //
    if (cmd) {
        if (isatty(1)) {
            addcmd("|");
            addcmd(pager);
        }
        fflush(stdout);
        system(cmd);
    }
    return notfound;
}
