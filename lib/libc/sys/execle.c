/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * execle(name, arg0, ..., (char *)0, envp) -- exec with an environment of one's own.
 *
 * The vector is the caller's argument list in place, as in execl() -- see the note
 * there -- and the only work is finding where it ends, because the word AFTER the
 * terminating null is the environment pointer.  v7 had to write that walk in assembly
 * (sys/execle.s); over <stdarg.h> it is a loop.
 *
 * THE TERMINATOR IS READ AS A RAW WORD, not through a char *.  A null argv slot is a
 * zero word, but a `char *' is a fat pointer -- bit 48 set, byte offset in bits 47-45
 * -- so re-reading that zero word as one would decorate it into a nonzero value and
 * the walk would never stop.  The printf engine reads %s the same way and for the same
 * reason (stdio/doprnt.c).  `envp' is safe to read as a pointer: a char ** is a plain
 * word address and carries no marker.
 */
#include <stdarg.h>

int exece(const char *name, char **argv, char **envp);

int execle(const char *name, ...)
{
    va_list ap;
    char **argv, **envp;

    va_start(ap, name);
    argv = (char **)ap;
    while (va_arg(ap, long) != 0) /* raw word: the caller's (char *)0 */
        ;
    envp = va_arg(ap, char **);
    va_end(ap);
    return exece(name, argv, envp);
}
