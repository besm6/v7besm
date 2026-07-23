// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// exit -- clean up and terminate.  v7 calls the file cuexit.s ("clean-up exit") and
// the name is kept: `exit.c' would collide with sys/exit.S in the flat object
// directory the library builds into.
//
// The one thing exit() does that _exit() does not is flush stdio.  That is why
// crt0.s jumps here and not to the bare trap, and why a program that really wants
// to skip the flush calls _exit() itself.
//
// IT GOES THROUGH A POINTER, and the pointer is why `hello' is not 2200 words long.
// Calling _cleanup() outright would make cuexit.o reference it, and cuexit.o is
// linked into EVERY program -- crt0.s tail-jumps here -- so every program would drag
// in flsbuf.o, and behind it _iob, the two 512-word buffers, malloc, free and close,
// whether it ever printed anything or not.  v7 makes exactly that bargain.
//
// A linker cannot get us out of it: b6ld pulls an archive member only for a symbol
// that is still UNDEFINED (load_ranlib_members() in cmd/ld/library.c), so a weak
// no-op here would satisfy the reference and stop the real _cleanup from ever being
// pulled -- which is the opposite of what is wanted, and is true of weak definitions
// generally, not just of this linker.
//
// So stdio arms the pointer itself, on the first buffered write (stdio/flsbuf.c).
// Nothing else can have anything to flush, so nothing else needs to arm it, and a
// program that never writes through a FILE never mentions _cleanup at all.  The
// whole cost to such a program is this one word of bss and the test below.
//
// When atexit() lands this becomes a special case of it -- stdio would register
// _cleanup like any other handler -- but a single word is cheaper than an atexit
// table in every program, and atexit is not written yet.
//
#include <stdlib.h>

_Noreturn void _exit(int status);

//
// Armed by _flsbuf() the first time a stream buffers anything.  It is a plain word
// pointer -- function pointers carry a 15-bit text address and are not fat -- so a
// null one really is a zero word and this test means what it says.
//
void (*_cleanup_hook)(void);

_Noreturn void exit(int status)
{
    if (_cleanup_hook != NULL)
        _cleanup_hook();
    _exit(status);
}
