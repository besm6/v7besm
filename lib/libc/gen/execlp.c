// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// execlp(name, arg0, ..., (char *)0) -- execl(), but searching $PATH for the file.
//
// The arguments are the vector, as in execl(): they sit in one contiguous parameter
// block and the caller wrote the terminating null itself, so the va_list is a
// NULL-terminated char *[] already.  See sys/execl.c for the whole of why.
//
// v7 kept this in the same file as execvp(); it is split out so that a program calling
// only execvp() does not drag it in, and vice versa.
//
#include <stdarg.h>

int execvp(const char *name, char **argv);

int execlp(const char *name, ...)
{
    va_list ap;
    char **argv;

    va_start(ap, name);
    argv = (char **)ap;
    va_end(ap);
    return execvp(name, argv);
}
