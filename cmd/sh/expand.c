/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// File name generation.
//
//   "*"          in params matches r.e ".*"
//   "?"          in params matches r.e. "."
//   "[...]"      in params matches character class
//   "[...a-z...]" in params matches a through z.
//
// THE DIRECTORY FORMAT IS NOT v7's.  v7 read sixteen bytes at a time -- a two-byte
// i-number and a fourteen-byte name -- and this file said so twice, with its own
// `#define DIRSIZ 15' and a literal 16 in the read().  A struct direct here is FOUR
// WORDS: one of i-number and three of name, so that DIRPB of them tile a 512-word block
// exactly (include/sys/param.h, and the reasoning is in include/sys/dir.h).  Both
// numbers now come from the header, so a change there reaches this file.
//
#include <fcntl.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "defs.h"

// Defined below.
static void addg(STRING as1, STRING as2, STRING as3);

//
// Expand one word against the file system, and return how many names came out.
//
// `as' is a word such as "/usr/*/bin/x".  If it holds no wildcard the answer is 0 and
// the caller uses the word as written.  Otherwise the word is split at the last slash,
// the directory part is opened, every entry matching the pattern part is added to the
// argument chain, and -- if there was a slash AFTER the wildcard -- each of those
// results is expanded again, which is what `rflg' and the `rescan' variable are for.
//
INT expand(STRING as, INT rflg)
{
    INT count, dirf;
    BOOL dir      = 0;
    STRING rescan = 0;
    STRING s, cs;
    ARGPTR schain = gchain;
    struct direct entry;
    STATBUF statb;

    if (trapnote & SIGSET)
        return 0;

    s = cs                   = as;
    entry.d_name[DIRSIZ - 1] = 0; // to end the string

    // check for meta chars
    {
        BOOL slash;
        slash = 0;
        while (!fngchar(*cs)) {
            if (*cs++ == 0) {
                if (rflg && slash)
                    break;
                else
                    return 0;
            } else if (*cs == '/') {
                slash++;
            }
        }
    }

    for (;;) {
        if (cs == s) {
            s = nullstr;
            break;
        } else if (*--cs == '/') {
            *cs = 0;
            if (s == cs)
                s = "/";
            break;
        }
    }
    if (stat(s, &statb) >= 0 && (statb.st_mode & S_IFMT) == S_IFDIR &&
        (dirf = open(s, O_RDONLY)) > 0)
        dir++;

    count = 0;
    if (*cs == 0)
        *cs++ = 0200;
    if (dir) {
        // check for rescan
        STRING rs;
        rs = cs;

        do {
            if (*rs == '/') {
                rescan = rs;
                *rs    = 0;
                gchain = 0;
            }
        } while (*rs++);

        while (read(dirf, (char *)&entry, DIRENTSZ) == DIRENTSZ && (trapnote & SIGSET) == 0) {
            if (entry.d_ino == 0 || (*entry.d_name == '.' && *cs != '.'))
                continue;
            if (gmatch(entry.d_name, cs)) {
                addg(s, entry.d_name, rescan);
                count++;
            }
        }
        close(dirf);

        if (rescan) {
            ARGPTR rchain;
            rchain = gchain;
            gchain = schain;
            if (count) {
                count = 0;
                while (rchain) {
                    count += expand(rchain->argval, 1);
                    rchain = rchain->argnxt;
                }
            }
            *rescan = '/';
        }
    }

    {
        CHAR c;
        s = as;
        while ((c = *s) != 0)
            *s++ = (c & STRIP ? c : '/');
    }
    return count;
}

//
// Match the name `s' against the pattern `p'.
//
// v7 wrote this with four recursive tail calls, which cost one stack frame per
// CHARACTER of an unbounded pattern.  The user stack here is four pages -- 4096 words
// (include/sys/param.h: USTKPAGE 28, NPAGE 32) -- and a pattern of a few hundred
// characters would run off the end of it with no diagnostic.  The three tail calls are
// loop iterations now.  The fourth, in `*', is not a tail call and stays: its depth is
// the number of stars in the pattern, since each recursion begins past one.
//
INT gmatch(STRING s, STRING p)
{
    INT scc;
    CHAR c;

    for (;;) {
        scc = *s++;
        if (scc) {
            if ((scc &= STRIP) == 0)
                scc = 0200;
        }

        switch (c = *p++) {
        case '[': {
            BOOL ok;
            INT lc;
            ok = 0;
            lc = 077777;
            while ((c = *p++) != 0) {
                if (c == ']') {
                    if (!ok)
                        return 0;
                    goto again; // v7: return gmatch(s,p)
                } else if (c == MINUS) {
                    if (lc <= scc && scc <= (*p++))
                        ok++;
                } else {
                    if (scc == (lc = (c & STRIP)))
                        ok++;
                }
            }
            return 0;
        }

        default:
            if ((c & STRIP) != scc)
                return 0;
            // FALLTHROUGH -- v7's `default' falls into `?', and that is what makes an
            // ordinary character consume one character of the name.

        case '?':
            if (!scc)
                return 0;
            goto again; // v7: return gmatch(s,p)

        case '*':
            if (*p == 0)
                return 1;
            --s;
            while (*s) {
                if (gmatch(s++, p))
                    return 1;
            }
            return 0;

        case 0:
            return scc == 0;
        }

    again:;
    }
}

//
// Add one generated name -- directory `as1', entry `as2', rescan tail `as3' -- to the
// chain of arguments.
//
static void addg(STRING as1, STRING as2, STRING as3)
{
    STRING s1, s2;
    INT c;

    s2 = locstak() + BYTESPERWORD;

    s1 = as1;
    while ((c = *s1++) != 0) {
        chkstak(s2);
        if ((c &= STRIP) == 0) {
            *s2++ = '/';
            break;
        }
        *s2++ = c;
    }
    s1 = as2;
    do {
        chkstak(s2);
    } while ((*s2 = *s1++) != 0 && (s2++, 1));
    if ((s1 = as3) != 0) {
        chkstak(s2);
        *s2++ = '/';
        do {
            chkstak(s2);
        } while ((*s2++ = *++s1) != 0);
    }
    makearg(endstak(s2));
}

//
// Put one finished word on the front of the chain of arguments being built for this
// command.  scan() later walks the chain backwards to produce the argv.
//
void makearg(STRING args)
{
    ((ARGPTR)args)->argnxt = gchain;
    gchain                 = (ARGPTR)args;
}
