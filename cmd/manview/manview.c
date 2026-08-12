//
// manview -- format a manual page in the dialect ../../doc/Manual_Page_Format.md describes.
//
//      manview [ -p ] [ -m ] [ -w cols ] [ file ... ]
//
// Task C25a (../README.md), and the thing man(1) has been arranged around since C25b: the one
// FORMATTER line in ../man/man.c is what puts this program between a page and the pager.
//
// ATTRIBUTES ARE ON BY DEFAULT AND ARE NOT CONDITIONED ON isatty(2), which is the one
// decision here that looks wrong until the pipeline is written out.  man builds
// `manview page | /bin/more' and hands it to system(3), so this program's standard output
// is a PIPE whenever a human is reading -- an isatty() test would suppress colour in
// exactly the case it is wanted and light it up in none.  A caller who wants clean bytes
// says -p, and `man - page' is the page source itself, which is what that flag was defined
// for in C25b and is why it survives this program's arrival.
//
// ONE BACK END, and it is ANSI.  Section 10 of the format calls backspace overstrike the
// default and the fallback -- that is nroff's world, and ../col/col.c exists to undo it.
// Nothing on this machine produces overstrike and nothing but col would read it, while
// ../more and ../novi already write hard-coded ANSI/VT100 and so does this.  -p is the
// fallback here, and it is a better one: it is legible in a file.
//
// THREE THINGS ARE NOT HERE and each is a decision.  No -h/HTML back end: there is no
// browser on a BESM-6 and one output is what this disk has room for.  No pager of its own:
// /bin/more is on the image and man already pipes into it.  And no width from the
// terminal: there is no TIOCGWINSZ in this kernel and no winsize to ask for, so -w is how
// a caller says what it knows.  What it says is the width of the TERMINAL: render.c leaves
// the rightmost column empty and fills the text to one less.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "manview.h"

static int plain; // -p: no escape sequences at all
static int mono;  // -m: attributes, no colour
static int width = DEFWIDTH;
static int status;

static void usage(void)
{
    fputs("usage: manview [-p] [-m] [-w cols] [file ...]\n", stderr);
    exit(1);
}

//
// The option parser, in place of getopt(3), which this libc has not got.  ../ls/ls.c is
// the model and ../man/man.c copied it before this did; unlike man's, a lone `-' is an
// OPERAND here and names the standard input.
//
static int options(int argc, char **argv)
{
    int i, ch;
    char *p;

    for (i = 1; i < argc; i++) {
        p = argv[i];
        if (p[0] != '-' || p[1] == '\0')
            break;
        if (p[1] == '-' && p[2] == '\0') {
            i++;
            break;
        }
        for (;;) {
            ch = *++p;
            if (ch == '\0')
                break;
            if (ch == 'p') {
                plain++;
            } else if (ch == 'm') {
                mono++;
            } else if (ch == 'w') {
                // The rest of the cluster if there is one, else the next argument.
                if (p[1] != '\0')
                    width = atoi(p + 1);
                else if (++i < argc)
                    width = atoi(argv[i]);
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

static void one(FILE *fp, const char *path)
{
    if (page(fp, path) < 0)
        status = 1;
}

int main(int argc, char **argv)
{
    FILE *fp;
    int i, n;

    i = options(argc, argv);
    if (width < MINWIDTH)
        width = MINWIDTH;
    if (width > MAXWIDTH)
        width = MAXWIDTH;
    out_setup(width, plain, mono);

    if (i >= argc) {
        one(stdin, "-");
        fflush(stdout);
        return status;
    }

    // man -a hands this program every page that matched, so more than one is the ordinary
    // case and not a convenience: a blank line goes between them and each keeps its banner.
    for (n = 0; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (n++ > 0)
                out_blank();
            one(stdin, "-");
            continue;
        }
        fp = fopen(argv[i], "r");
        if (fp == 0) {
            fflush(stdout); // stderr is unbuffered: the two must interleave as written
            perror(argv[i]);
            status = 1;
            continue;
        }
        if (n++ > 0)
            out_blank();
        one(fp, argv[i]);
        fclose(fp);
    }
    fflush(stdout);
    return status;
}
