// struct _dirdesc -- what a DIR really is.  Private to the seven directory objects
// beside this file; <dirent.h> publishes only the typedef.  lib/libcurses/internal.h is
// the precedent for a header that lives inside a library rather than in include/.
//
// THE CURSOR IS AN ENTRY INDEX, NOT A BYTE OFFSET, and that is the one decision here
// that is about this machine rather than about directories.  4.2BSD keeps `char *dd_buf'
// with a byte offset dd_loc and forms (struct direct *)(dd_buf + dd_loc).  A char * is a
// FAT pointer here and that cast FLOORS to the containing word, which happens to be right
// only because DIRENTSZ is a multiple of NBPW.  Indexing a struct direct * array does the
// same arithmetic with no cast, no byte offset and no dependence on that coincidence --
// and malloc guarantees every block starts at byte #0 of a word (gen/malloc.c), so the
// array is aligned by construction.  The assertion below is what makes a future retune of
// DIRSIZ a diagnostic instead of silent garbage.
//
// TWELVE WORDS, and the read buffer is NOT one of them: it is a second malloc, sized in
// opendir() from the directory's own st_size and capped at one filesystem block.  A fixed
// one-block buffer inside the struct would cost 512 words for a /dev of twenty-five
// entries, and _NFILE is 20 -- seventeen open directories would be 8,704 words of a
// 28,672-word address space.  See directory(3).

#ifndef _DIRDESC_H
#define _DIRDESC_H

#include <dirent.h>
#include <sys/dir.h>
#include <sys/param.h>

_Static_assert(DIRENTSZ % NBPW == 0, "a struct direct must be a whole number of words");

struct _dirdesc {
    int dd_fd;             // the descriptor open on the directory
    int dd_ent;            // index in dd_buf of the next entry readdir() will look at
    int dd_nent;           // entries the last read(2) actually delivered
    int dd_max;            // entries dd_buf can hold
    long dd_off;           // byte offset in the file of dd_buf[0]
    struct direct *dd_buf; // dd_max on-disk entries
    struct dirent dd_ret;  // what readdir() returns a pointer to
};

#endif // _DIRDESC_H
