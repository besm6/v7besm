//
// strerror(errnum) -- the message the error number `errnum' names (C11 §7.24.6.2).
//
// v7 had no such routine: a program that wanted the text indexed sys_errlist[] itself,
// unchecked, which is what cmd/kill did and what cmd/sh/service.c had to fix in its own
// signal-name table.  <string.h> has declared this since the header tree went C11 and
// gen/errlst.c's header comment has said all along that strerror() and perror() are the
// two things the table exists for -- but nothing defined it, and nothing caught that:
// lib/test/headers.c includes every header twice and a declaration with no definition
// links perfectly well until someone calls it.  cmd/kill (task C2a) is the first caller
// there has ever been.
//
// The table is the one perror() prints, so the two cannot disagree; perror() is written in
// terms of this now rather than repeating the bound test.
//
// THE RESULT MUST NOT BE MODIFIED (§7.24.6.2p3) and is not a copy: it points into
// sys_errlist[] or at a literal.  The return type is char * rather than const char *
// because that is what C11 specifies and what <string.h> declares.
//
#include <string.h>

extern int sys_nerr;
extern char *sys_errlist[];

char *strerror(int errnum)
{
    if (errnum < 0 || errnum >= sys_nerr)
        return "Unknown error";
    return sys_errlist[errnum];
}
