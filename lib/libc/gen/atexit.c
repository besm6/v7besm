//
// atexit -- register a function to be called at normal program termination.
//
// C11 §7.22.4.2, and <stdlib.h> has declared it since this tree's headers were
// written; nothing implemented it until cmd/cc wanted it (task C9e, cmd/TODO.md)
// to unlink its temporary files however the driver leaves.  v7 had no such thing:
// a v7 program that wanted cleanup on every path wrote its own exit() wrapper,
// which is what several in cmd/ still do.
//
// IT GOES THROUGH A HOOK, and cuexit.c's comment on _cleanup_hook is the whole
// argument: exit() is tail-jumped to by crt0 and is therefore linked into EVERY
// program, so anything it names outright is paid for by every program.  Calling
// the table below from exit() would put this file -- and its ATEXIT_MAX words of
// bss -- into `hello'.  So exit() tests a pointer, atexit() arms it with
// _atexit_run(), and a program that never registers anything pays the one word
// exit() already had to have.
//
// THE ORDER IS C11's, and it is not the order the two hooks are declared in:
// registered handlers run FIRST, in reverse order of registration, and stdio's
// flush comes after them, because a handler is allowed to print.  exit() spells
// that out.
//
#include <stdlib.h>

//
// C11 asks for at least 32 registrations, and this table is exactly that: a
// program on this machine gets 28,672 words for everything, and nothing here has
// ever wanted a second handler, let alone a thirty-third.
//
#define ATEXIT_MAX 32

static void (*handlers[ATEXIT_MAX])(void);
static int nhandlers;

extern void (*_atexit_hook)(void);

//
// Armed into _atexit_hook by the first atexit() call; run by exit() ahead of the
// stdio flush.  A handler that itself calls exit() is undefined behaviour in C11
// and would recurse here; nothing guards it, exactly as nothing guards the same
// thing in _cleanup.
//
static void _atexit_run(void)
{
    while (nhandlers > 0)
        (*handlers[--nhandlers])();
}

int atexit(void (*func)(void))
{
    if (func == NULL || nhandlers >= ATEXIT_MAX)
        return -1;
    handlers[nhandlers++] = func;
    _atexit_hook          = _atexit_run;
    return 0;
}
