// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// execl(name, arg0, arg1, ..., (char *)0) -- exec with the arguments written out.
//
// THE ARGUMENT LIST IS ALREADY THE argv[] VECTOR, and nothing is copied.  Arguments
// are pushed in direct order into one contiguous parameter block, every scalar is
// exactly one word, and <stdarg.h> defines va_start(ap, last) as `&last + 1' -- so
// the va_list points at the caller's arg0 slot and the words that follow are arg1,
// arg2, ... and the caller's own terminating null.  That is a NULL-terminated array
// of char * by construction, which is exactly what the gate wants.  v7 said the same
// thing as `&args' and could not say it portably; here it is just the va_list.
//
// exece(), and not exec(): v7's three-argument gate is sysent[59] and is spelled
// `exece' throughout this tree (sys/exece.S).  `environ' is crt0's, and passing it on
// is the whole difference between this and execle().
//
// A successful exec never returns, so there is only the failure path to hand back.
//
#include <stdarg.h>

extern char **environ;

int exece(const char *name, char **argv, char **envp);

int execl(const char *name, ...)
{
    va_list ap;
    char **argv;

    va_start(ap, name);
    argv = (char **)ap;
    va_end(ap);
    return exece(name, argv, environ);
}
