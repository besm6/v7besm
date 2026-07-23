/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * setgrent, endgrent, getgrent -- walk /etc/group, one entry at a time.
 *
 * The mirror of stdio/getpwent.c, down to splitting the line in place, and the line
 * buffer is sized the same way and for the same reason: GRLINE, not BUFSIZ, which is
 * 3072 here and describes a disk block rather than a line of text.
 *
 * The member list is the one thing /etc/passwd has no counterpart to: the last field
 * is comma-separated and gr_mem points at a vector built over it.  ONE FIX: v7 sized
 * that vector MAXGRP and then filled it without checking, so a group line with more
 * members than that walked off the end of a file-scope array into whatever bss
 * followed.  Here the walk stops with room for the terminator and the tail is dropped.
 * A 512-byte line cannot hold a hundred names anyway; the bound is the array's, not
 * the format's.
 */
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>

#define GRLINE 512
#define MAXGRP 100

#define CL ':'
#define CM ','
#define NL '\n'

static const char GROUP[] = "/etc/group";

static FILE *grf = NULL;
static char line[GRLINE + 1];
static struct group group;
static char *gr_mem[MAXGRP];

void setgrent(void)
{
    if (grf == NULL)
        grf = fopen(GROUP, "r");
    else
        rewind(grf);
}

void endgrent(void)
{
    if (grf != NULL) {
        fclose(grf);
        grf = NULL;
    }
}

/* Terminate the field p points at at the first `c' and return the next one. */
static char *grskip(char *p, int c)
{
    while (*p && *p != c)
        ++p;
    if (*p)
        *p++ = 0;
    return p;
}

struct group *getgrent(void)
{
    char *p, **q;

    if (grf == NULL) {
        if ((grf = fopen(GROUP, "r")) == NULL)
            return NULL;
    }
    if ((p = fgets(line, GRLINE, grf)) == NULL)
        return NULL;

    group.gr_name   = p;
    group.gr_passwd = p = grskip(p, CL);
    group.gr_gid        = atoi(p = grskip(p, CL));
    group.gr_mem        = gr_mem;
    p                   = grskip(p, CL);
    grskip(p, NL);

    q = gr_mem;
    while (*p) {
        if (q >= &gr_mem[MAXGRP - 1])
            break;
        *q++ = p;
        p    = grskip(p, CM);
    }
    *q = NULL;
    return &group;
}
