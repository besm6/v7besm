/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The stream table and the two static buffers, v7's arrangement exactly.
 *
 * _iob[0..2] are stdin, stdout and stderr.  stdin starts pointed at _sibuf with a
 * zero count, so the first getc() misses and _filbuf() fills the buffer already in
 * place; stdout gets _sobuf lazily, in _flsbuf(), because a program that never
 * writes should not be charged for deciding how.  stderr is unbuffered outright --
 * an error message must reach the terminal before whatever goes wrong next.
 *
 * BUFSIZ is 3072 BYTES, one disk block: BSIZE == BSIZEW * NBPW == 512 words, and
 * file I/O counts bytes throughout (kernel/rdwri.c).  Six chars to a word, so the
 * two buffers together are 1024 words of bss out of the 32-page address space.
 *
 * _IOUNBUF is v7's _IONBF: the bit kept its value and gave up its name, because C11
 * needs _IONBF for a setvbuf mode (include/stdio.h).
 */
#include <stdio.h>

char _sibuf[BUFSIZ];
char _sobuf[BUFSIZ];

FILE _iob[_NFILE] = {
    { _sibuf, 0, _sibuf, BUFSIZ, _IOREAD, 0 },
    { NULL, 0, NULL, 0, _IOWRT, 1 },
    { NULL, 0, NULL, 0, _IOWRT | _IOUNBUF, 2 },
};

/* Ptr to end of buffers */
FILE *_lastbuf = &_iob[_NFILE];
