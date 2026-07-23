/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * exit -- clean up and terminate.  v7 calls the file cuexit.s ("clean-up exit") and
 * the name is kept: `exit.c' would collide with sys/exit.S in the flat object
 * directory the library builds into.
 *
 * The one thing exit() does that _exit() does not is run _cleanup(), which fcloses
 * every stream and so flushes whatever printf left in a buffer (stdio/flsbuf.c).
 * That is why crt0.s jumps here and not to the bare trap, and why a program that
 * really wants to skip the flush calls _exit() itself.
 *
 * The cost is that any program calling exit() -- which is every program, since
 * crt0.s tail-jumps to it -- links the stdio machinery whether it prints or not.
 * v7 makes the same bargain, and there is no way to make it conditional: a static
 * linker pulling members by symbol has no notion of "only if something else needed
 * it".
 */
#include <stdlib.h>

_Noreturn void _exit(int status);

void _cleanup(void);

_Noreturn void exit(int status)
{
    _cleanup();
    _exit(status);
}
