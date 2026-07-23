// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// tell(f) -- the current offset in an open file.
//
// A library routine, and only that: v7 had a `tell' SYSTEM CALL once, and by v7 it was
// already gone -- sysent[40] is nosys on this kernel too (kernel/sysent.c), and
// include/sys/syscall.h names no SYS_tell for exactly that reason.  So this is the
// seek that replaced it, spelled the old way for the programs that still say it.
//
// off_t is one word, so v7's `long tell()' and its `0L' are an int and a 0 here.
//
// No header declares it; a caller declares it itself.
//
#include <sys/types.h>

off_t lseek(int fd, off_t off, int whence);

off_t tell(int f)
{
    return lseek(f, 0, 1);
}
