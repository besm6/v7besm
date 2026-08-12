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
// THE DIRECTORY IS READ WITH opendir(3), task C24, and that fixed a live bug: v7's
// `entry.d_name[DIRSIZ-1] = 0' terminated the name once, before the loop, which worked only
// because v7 read sixteen bytes into a fifteen-byte DIRSIZ.  Both numbers changed under it
// here and the write landed INSIDE what every read() overwrites, so an 18-character name
// reached gmatch() unterminated.  README.md has the account.  The loop still tests
// trapnote & SIGSET between entries, which is what keeps a globbing shell interruptible.
//
#include <dirent.h>
#include <sys/types.h>

#include "defs.h"

// Defined below.
static void addg(STRING as1, BOOL slash, STRING as2, STRING as3);
static STRING plainstak(STRING as);

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
    INT count;
    DIR *dirp     = 0;
    BOOL slash    = 0;
    STRING rescan = 0;
    STRING s, cs;
    INT slashpos;
    ARGPTR schain = gchain;
    struct dirent *dp;

    if (trapnote & SIGSET)
        return 0;

    //
    // Find the first unquoted filename metacharacter, and remember where the last unquoted
    // `/' in front of it was.
    //
    // v7 did this in two passes, the second of them SCANNING BACK from the metacharacter
    // for the slash.  Backwards is not available here: in the stored form (defs.h) a byte
    // is a payload or a mark according to what stands in front of it, so a `/' met while
    // walking down cannot be told from the payload of a quoted one.  One forward pass
    // answers both questions and needs no second scan.
    //
    {
        STRING p = as;
        STRING here;
        INT c;

        slashpos = -1;
        for (;;) {
            here = p;
            c    = nextq(&p);
            if (c == 0) {
                //
                // No metacharacter at all.  Ordinarily that means the word stands as
                // written; on a RESCAN it means the tail after the last slash is a plain
                // name, which still has to be looked up in the directory in front of it.
                //
                if (rflg && slashpos > 0)
                    break;
                return 0;
            }
            if (c == '/') {
                slashpos = here - as;
            } else if (fngchar(c)) {
                break;
            }
        }
    }

    //
    // Split the word at that slash: `s' is the directory to read and `cs' the pattern to
    // match its entries against.  The NUL is written back as a `/' at the end -- v7 left a
    // 0200 there instead, a quoted NUL, which is one of the two things bit 0200 meant in
    // this file and neither of them survives an eight-bit character.
    //
    if (slashpos < 0) {
        s  = nullstr;
        cs = as;
    } else if (slashpos == 0) {
        s  = "/";
        cs = as + 1;
    } else {
        as[slashpos] = 0;
        s            = as;
        cs           = as + slashpos + 1;
        slash        = 1;
    }

    {
        //
        // opendir() wants a real name, not the stored form, and a path hardly ever has
        // anything in it to decode -- so the copy is made only when it does.  v7 handed the
        // marked text straight to stat(), which is why `"/usr"/bin/*' globbed nothing there;
        // it does here.  opendir() also makes the separate S_IFDIR test its own business.
        //
        STRING plain = any(QESC, s) ? plainstak(s) : s;

        dirp = opendir(plain);
    }

    count = 0;
    if (dirp) {
        // check for rescan
        STRING rs = cs;
        STRING here;

        for (;;) {
            here = rs;
            if (nextq(&rs) == 0)
                break;
            if (*here == '/') {
                //
                // An unquoted `/' inside the pattern: everything past it is matched again,
                // against each of the names this pass turns up.  nextq() is what makes the
                // test `unquoted' -- a `\/' is one character here and its payload is not
                // at `here'.
                //
                rescan = here;
                *here  = 0;
                gchain = 0;
                break;
            }
        }

        while ((dp = readdir(dirp)) != NULL && (trapnote & SIGSET) == 0) {
            STRING p = cs;

            //
            // A leading dot has to be asked for.  The test is against the DECODED first
            // character of the pattern, but deliberately not against its payload: a
            // quoted dot carries the mark and so does not count, exactly as v7's 0256 did
            // not equal '.'.  So `\.*' still matches nothing, which is v7's answer and
            // not obviously the right one -- it is kept because changing it is a change
            // to the pattern language and belongs to whoever wants it, not to task C11.
            //
            if (*dp->d_name == '.' && nextq(&p) != '.')
                continue;
            if (gmatch(dp->d_name, cs)) {
                addg(s, slash, dp->d_name, rescan);
                count++;
            }
        }
        closedir(dirp);

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

    if (slash)
        as[slashpos] = '/';
    return count;
}

//
// Copy a string onto the stack with the quoting marks taken off, and return it.
//
// Used for the one place a piece of a still-encoded word has to be handed to the system as
// a name.  trim() would do the same job in place, but in place is exactly what is not
// available: the word is about to be put back together again.
//
static STRING plainstak(STRING as)
{
    STRING s   = as;
    STRING out = locstak();
    INT c;

    while ((c = nextq(&s)) != 0) {
        chkstak(out);
        *out++ = c;
    }
    return endstak(out);
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
    INT c;

    for (;;) {
        //
        // THE SUBJECT IS A RAW NAME -- a directory entry, or the `case' word after
        // mactrim() -- so its bytes are taken as they stand.  v7 masked each one with 0177
        // and then mapped a resulting 0 back to 0200, which is what made `case' and
        // globbing agree that а (0320 0260) and P0 were the same two characters.
        //
        scc = *s++;

        c = nextq(&p);
        if (c & QUOTE) {
            //
            // A quoted pattern character matches itself and nothing else -- that is the
            // whole point of quoting one -- so `*', `?', `[' and the rest never get here.
            //
            if ((c & LOBYTE) != scc || scc == 0)
                return 0;
            goto again;
        }

        switch (c) {
        case '[': {
            BOOL ok;
            INT lc;
            ok = 0;
            lc = 077777;
            while ((c = nextq(&p)) != 0) {
                if (c == ']') {
                    if (!ok)
                        return 0;
                    goto again; // v7: return gmatch(s,p)
                } else if (c == MINUS) {
                    //
                    // v7 fetched the top of the range with a bare *p++, which walks past
                    // the terminator on an unfinished `[a-'.  nextq() reports the end of
                    // the string and this stops there.
                    //
                    INT hc = nextq(&p);
                    if (hc == 0)
                        return 0;
                    if (lc <= scc && scc <= (hc & LOBYTE))
                        ok++;
                } else {
                    if (scc == (lc = (c & LOBYTE)))
                        ok++;
                }
            }
            return 0;
        }

        default:
            if (c != scc)
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
static void addg(STRING as1, BOOL slash, STRING as2, STRING as3)
{
    STRING s1, s2;
    INT c;

    s2 = locstak() + BYTESPERWORD;

    //
    // THE DIRECTORY PART IS COPIED AS IT STANDS.  It is already stored text and the word
    // being built is stored text too: it goes back on the argument chain, to be decoded by
    // scan()'s trim() or expanded a second time by the rescan.  v7 took the marks off here
    // and let the later trim() find nothing to do; that is not available now, because
    // decoding twice eats the byte after every QESC.
    //
    // The `/' is written out rather than found.  v7 arrived at it by running off the end
    // of the directory string into the 0200 it had left where the slash was -- see
    // expand() above, where that sentinel used to be.
    //
    s1 = as1;
    while ((c = *s1++) != 0) {
        chkstak(s2);
        *s2++ = c;
    }
    if (slash) {
        chkstak(s2);
        *s2++ = '/';
    }
    //
    // The directory ENTRY, on the other hand, is a raw name off the disk, so it is encoded
    // -- otherwise a 0377 in a file name would be read back as a quoting mark by the very
    // trim() that is meant to hand this word to a command.
    //
    s1 = as2;
    while ((c = *s1++) != 0)
        s2 = putq(s2, c);
    if ((s1 = as3) != 0) {
        // The rescan tail is untouched pattern text: stored form again.
        chkstak(s2);
        *s2++ = '/';
        while ((c = *++s1) != 0) {
            chkstak(s2);
            *s2++ = c;
        }
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
