/*
 * tmpfile -- a temporary binary file, gone when it is closed (C11 §7.21.4.3).
 * Not a v7 routine.
 *
 * The name is unlinked the moment the stream holds the descriptor, so the file has
 * no name from then on and the kernel frees the inode when the last reference goes
 * -- which is either fclose() or the _cleanup() that exit() runs.  Nothing is left
 * behind even if the program dies on a signal.
 */
#include <stdio.h>

int unlink(const char *path);

FILE *tmpfile(void)
{
    char name[L_tmpnam];
    FILE *iop;

    if (tmpnam(name) == NULL)
        return NULL;
    if ((iop = fopen(name, "w+")) == NULL)
        return NULL;
    unlink(name);
    return iop;
}
