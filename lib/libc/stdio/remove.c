/*
 * remove -- delete a file (C11 §7.21.4.1).  Not a v7 routine; v7 code calls
 * unlink() outright, which is all this is.
 */
#include <stdio.h>

int unlink(const char *path);

int remove(const char *path)
{
    return unlink(path);
}
