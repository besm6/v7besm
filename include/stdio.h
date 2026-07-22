// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <stdio.h> -- input/output (C11 §7.21).
//
// Nothing here is implemented yet.  The FILE machinery is lib phase 4, straight
// from the v7 sources still sitting in lib/tmp/libc/stdio/, with the formatting
// engine taken from the c-compiler's doprnt.c instead of v7's x86 assembly.
// Until then the only routine in libc.a that this header declares is perror(),
// which writes to fd 2 with write() precisely because there is no stdio yet.
//
// v7's header declared five functions, all with empty parens, and left the rest
// to K&R's implicit int -- so it did not declare printf at all.  That is no
// longer merely untidy: the front end has no implicit declarations, so a phase-4
// caller would not have compiled.  Everything §7.21 lists is declared below.
//
// The FILE structure is v7's, unchanged, because the phase-4 sources are v7's
// and index it directly: _ptr/_cnt/_base/_flag/_file, and _iob[_NFILE] with
// stdin/stdout/stderr as its first three slots.  Only the SPELLING changed --
// `#define FILE struct _iobuf' became a typedef, since §7.21.1 wants FILE to be
// an object type and a macro cannot be one (nor can it be written `FILE *' in a
// declaration that a `struct _iobuf' typedef would also match).
//
// BUFSIZ is one disk block in BYTES: BSIZE == BSIZEW * NBPW == 512 words
// (sys/param.h), and file I/O counts bytes throughout (kernel/rdwri.c).
//
// One collision to know about.  v7 used the name _IONBF for a BIT in _flag; C11
// uses it for a setvbuf MODE, and the two cannot be the same number.  The flag
// bit keeps its value under the name _IOUNBUF, and _IOFBF/_IOLBF/_IONBF are the
// three modes.  Phase 4 must spell the bit _IOUNBUF when it ports filbuf.c,
// flsbuf.c and setbuf.c, which are the only three files that touch it.
#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h> // size_t, NULL

// One block, in bytes: BSIZE == BSIZEW * NBPW == 512 words (sys/param.h).
#define BUFSIZ 3072
#define _NFILE 20

typedef struct _iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    char _flag;
    char _file;
} FILE;

extern FILE _iob[_NFILE];

// A file position.  v7 had no fpos_t and used long for a file offset; here that
// is one word, the same off_t the kernel counts bytes in (sys/types.h).
typedef int fpos_t;

// _flag bits.
#define _IOREAD  01
#define _IOWRT   02
#define _IOUNBUF 04 // v7 called this _IONBF; see the note above
#define _IOMYBUF 010
#define _IOEOF   020
#define _IOERR   040
#define _IOSTRG  0100
#define _IORW    0200

// setvbuf modes (§7.21.5.6).  Not _flag bits -- see the note above.
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// FOPEN_MAX is _NFILE, which is also NOFILE in sys/param.h -- one FILE per
// descriptor and no more.  There is no path-length limit in this kernel: namei()
// walks a component at a time and only the component is bounded, by DIRSIZ.  So
// FILENAME_MAX is a convention for sizing a caller's buffer, not a kernel
// number, and 128 is v7's customary one.  TMP_MAX is what mktemp() can actually
// distinguish: three letters, 26^3.
#define FOPEN_MAX    _NFILE
#define FILENAME_MAX 128
#define L_tmpnam     20
#define TMP_MAX      17576

// Declarations first, macros after: a macro defined ahead of the declaration it
// shares a name with would eat it.

// ---- implemented in libc.a ----
void perror(const char *s);

// ---- declared for future implementation: lib phase 4 (TODO) ----
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *iop);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *iop);
int fflush(FILE *iop);

int printf(const char *fmt, ...);
int fprintf(FILE *iop, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *iop, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int scanf(const char *fmt, ...);
int fscanf(FILE *iop, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int vfscanf(FILE *iop, const char *fmt, va_list ap);
int vsscanf(const char *str, const char *fmt, va_list ap);

int fgetc(FILE *iop);
int fputc(int c, FILE *iop);
int getc(FILE *iop);
int putc(int c, FILE *iop);
int getchar(void);
int putchar(int c);
int ungetc(int c, FILE *iop);

char *fgets(char *s, int n, FILE *iop);
int fputs(const char *s, FILE *iop);
char *gets(char *s);
int puts(const char *s);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *iop);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *iop);

int fseek(FILE *iop, long offset, int whence);
long ftell(FILE *iop);
void rewind(FILE *iop);
int fgetpos(FILE *iop, fpos_t *pos);
int fsetpos(FILE *iop, const fpos_t *pos);

void setbuf(FILE *iop, char *buf);
int setvbuf(FILE *iop, char *buf, int mode, size_t size);

void clearerr(FILE *iop);
int feof(FILE *iop);
int ferror(FILE *iop);

int remove(const char *path);
int rename(const char *from, const char *to);
FILE *tmpfile(void);
char *tmpnam(char *s);

// The internal pair the getc/putc macros fall out to, and v7's word-at-a-time
// pair, which are v7 extensions and not C11.
int _filbuf(FILE *iop);
int _flsbuf(int c, FILE *iop);
int getw(FILE *iop);
int putw(int w, FILE *iop);

#define stdin  (&_iob[0])
#define stdout (&_iob[1])
#define stderr (&_iob[2])

// §7.21.1p3 singles getc and putc out as the two library macros that MAY
// evaluate their stream argument more than once; every other macro here
// evaluates its argument exactly once, as §7.1.4 requires.
#define getc(p)   (--(p)->_cnt >= 0 ? *(p)->_ptr++ & 0377 : _filbuf(p))
#define getchar() getc(stdin)
#define putc(x, p) \
    (--(p)->_cnt >= 0 ? ((int)(*(p)->_ptr++ = (unsigned)(x))) : _flsbuf((unsigned)(x), p))
#define putchar(x) putc(x, stdout)
#define feof(p)    (((p)->_flag & _IOEOF) != 0)
#define ferror(p)  (((p)->_flag & _IOERR) != 0)

#define fileno(p) ((p)->_file) // POSIX, not C11; v7 has it and v7 code uses it

#endif // _STDIO_H
