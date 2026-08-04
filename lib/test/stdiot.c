//
// stdiot -- the FILE machinery of phase 4, driven through a real file.
//
// b6sim services open/read/write/lseek on the host, so a stream here goes all the
// way to a descriptor and back; that is the part `printft' and `scanft' cannot
// reach, since they format into memory or read what this side wrote.
//
// What is worth proving rather than merely transcribing:
//
//   getw/putw move SIX bytes.  v7's moved two, and the value below has all six
//   populated with distinct bytes -- an implementation that carried four of them,
//   or reassembled them in the other order, comes back with a different word.
//
//   fseek's fast path seeks WITHIN the buffer without troubling the kernel, and the
//   slow path does not; ftell has to agree with both, and with a write stream, where
//   the kernel's offset is behind the caller's by whatever is still buffered.
//
//   ungetc has no pushback slot of its own -- it steps _ptr back into the buffer --
//   so it is exercised at a buffer boundary as well as in the middle.
//
//   the three buffering modes all reach the same bytes.  The line-buffered one is
//   the new one (v7 had no _IOLBUF) and it is the odd one out mechanically: it is
//   held at _cnt == 0 so every putc misses into _flsbuf.
//
//   setbuffer hands over a buffer that is NOT BUFSIZ, which is what _bufsiz is for:
//   eight bytes with twenty written through them.
//
//   an _IOSTRG stream never touches a descriptor, which is why sscanf works on a
//   FILE with _file == -1.
//
//   open_memstream is the other kind of _IOSTRG stream and the only one that occupies
//   an _iob slot, so it is the only one whose release can be got wrong -- which is
//   what the slot-reuse case is for.  See the head comment of memstream() below.
//
// The scratch file is created here and removed at the end, so a run leaves nothing
// behind and the .expected file stays host-independent.
//
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FNAME "stdiot.tmp"
#define GNAME "stdiot2.tmp"

//
// A word with six distinct non-zero bytes.  It needs 45 bits, so it does NOT fit a
// signed int -- which is 41 bits here -- and that is the point: it is compared as an
// unsigned word, and an implementation that moved four bytes, or reassembled them in
// the other order, comes back with something else.
//
static unsigned wordval = 0x112233445566U;

static int errors;

static void ok(const char *what, int cond)
{
    printf("%-4s %s\n", cond ? "ok" : "FAIL", what);
    if (!cond)
        errors++;
}

static void eq(const char *what, long got, long want)
{
    if (got == want)
        printf("ok   %s %ld\n", what, got);
    else {
        printf("FAIL %s %ld, want %ld\n", what, got, want);
        errors++;
    }
}

// The same, for a value that fills all 48 bits and so does not fit a signed word.
static void equ(const char *what, unsigned got, unsigned want)
{
    if (got == want)
        printf("ok   %s %#x\n", what, got);
    else {
        printf("FAIL %s %#x, want %#x\n", what, got, want);
        errors++;
    }
}

static void eqs(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0)
        printf("ok   %s [%s]\n", what, got);
    else {
        printf("FAIL %s [%s], want [%s]\n", what, got, want);
        errors++;
    }
}

//
// Write the scratch file: three lines, then a word laid down by putw.  Everything
// goes through a different routine on purpose -- fputs, fprintf, fwrite, putc.
//
static void writefile(void)
{
    FILE *f;

    f = fopen(FNAME, "w");
    ok("fopen w", f != NULL);
    if (f == NULL)
        return;

    fputs("first line\n", f);
    fprintf(f, "%s %d\n", "second", 22);
    fwrite("third line\n", 1, 11, f);
    eq("ftell while buffered", ftell(f), 32);
    ok("fflush", fflush(f) == 0);
    eq("ftell after fflush", ftell(f), 32);
    putc('#', f);
    putw(wordval, f);
    ok("fclose", fclose(f) == 0);
}

// Read it back with every reading routine there is.
static void readfile(void)
{
    FILE *f;
    char buf[80];
    int c, n;
    fpos_t pos;

    f = fopen(FNAME, "r");
    ok("fopen r", f != NULL);
    if (f == NULL)
        return;

    fgets(buf, sizeof buf, f);
    eqs("fgets", buf, "first line\n");
    eq("ftell after fgets", ftell(f), 11);

    // fgets stops at the buffer size too, leaving the rest for the next call.
    fgets(buf, 5, f);
    eqs("fgets short", buf, "seco");
    fgets(buf, sizeof buf, f);
    eqs("fgets rest", buf, "nd 22\n");

    // getc/ungetc in the middle of the buffer.
    c = getc(f);
    eq("getc", c, 't');
    eq("ungetc", ungetc(c, f), 't');
    eq("getc again", getc(f), 't');

    n      = fread(buf, 1, 10, f);
    buf[n] = '\0';
    eq("fread count", n, 10);
    eqs("fread data", buf, "hird line\n");

    eq("getc '#'", getc(f), '#');
    equ("getw", getw(f), wordval);

    // Now at end of file: getc reports EOF and sets the flag, clearerr clears it.
    eq("getc at eof", getc(f), EOF);
    ok("feof", feof(f) != 0);
    ok("!ferror", ferror(f) == 0);
    clearerr(f);
    ok("clearerr", feof(f) == 0);

    // Seeking: absolute, relative, from the end, and the buffered fast path.
    ok("fseek set", fseek(f, 6L, SEEK_SET) == 0);
    eq("ftell after seek", ftell(f), 6);
    eq("getc after seek", getc(f), 'l');
    ok("fseek cur", fseek(f, 4L, SEEK_CUR) == 0);
    eq("getc after cur", getc(f), 's');
    ok("fseek end", fseek(f, -7L, SEEK_END) == 0);
    eq("ftell from end", ftell(f), 32);

    ok("fgetpos", fgetpos(f, &pos) == 0);
    eq("getc at pos", getc(f), '#');
    ok("fsetpos", fsetpos(f, &pos) == 0);
    eq("getc at pos again", getc(f), '#');

    rewind(f);
    eq("ftell after rewind", ftell(f), 0);
    eq("getc after rewind", getc(f), 'f');

    // ungetc at the very start of the buffer, where there is no room behind _ptr.
    rewind(f);
    eq("ungetc before any read", ungetc('X', f), 'X');
    eq("getc gets it back", getc(f), 'X');

    ok("fclose r", fclose(f) == 0);
}

// fdopen over a descriptor open() gave us, and freopen onto a live stream.
static void reopen(void)
{
    FILE *f;
    char buf[80];
    int fd;

    fd = open(FNAME, O_RDONLY);
    ok("open", fd >= 0);
    f = fdopen(fd, "r");
    ok("fdopen", f != NULL);
    if (f != NULL) {
        fgets(buf, sizeof buf, f);
        eqs("fdopen fgets", buf, "first line\n");
        //
        // The VALUE must not be printed: which descriptor open() hands out depends
        // on what the process already had open, and make's jobserver holds a pipe on
        // fd 3, so a number here would differ between `make test' and the same run by
        // hand.  Nothing host-dependent may reach an .expected file (lib/README.md).
        //
        ok("fileno agrees with open", fileno(f) == fd);

        f = freopen(FNAME, "r", f);
        ok("freopen", f != NULL);
        fgets(buf, sizeof buf, f);
        eqs("freopen fgets", buf, "first line\n");
        fclose(f);
    }
}

// Read the scratch file whole into buf, NUL-terminated, and return its length.
static int readback(char *buf, int size)
{
    FILE *f;
    int n;

    f      = fopen(FNAME, "r");
    n      = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

//
// The same six characters through each buffering mode.  Unbuffered spends a write()
// per byte, line buffered flushes on the newline, fully buffered flushes at fclose;
// all three must leave the same file.
//
static void buffering(void)
{
    static char mybuf[BUFSIZ];
    static char small[8]; // deliberately NOT BUFSIZ: see setbuffer below
    FILE *f;
    char buf[80];
    int mode, n;
    static const char *name[] = { "fully", "line", "un" };
    static const int modes[]  = { _IOFBF, _IOLBF, _IONBF };

    for (mode = 0; mode < 3; mode++) {
        f = fopen(FNAME, "w");
        ok("fopen for setvbuf", f != NULL);
        ok("setvbuf", setvbuf(f, modes[mode] == _IONBF ? NULL : mybuf, modes[mode], BUFSIZ) == 0);
        fputs("ab\ncd\n", f);
        fclose(f);

        n = readback(buf, sizeof buf);
        printf("%-4s %s buffered wrote %d bytes\n",
               (n == 6 && strcmp(buf, "ab\ncd\n") == 0) ? "ok" : "FAIL", name[mode], n);
        if (n != 6 || strcmp(buf, "ab\ncd\n") != 0)
            errors++;
    }

    // setbuf(f, NULL) is the old spelling of _IONBF, and must still work.
    f = fopen(FNAME, "w");
    setbuf(f, NULL);
    fputs("xy\n", f);
    fclose(f);
    readback(buf, sizeof buf);
    eqs("setbuf NULL", buf, "xy\n");

    //
    // The BSD pair.  setbuffer's whole reason to exist is a buffer that is NOT
    // BUFSIZ: eight bytes here with twenty written through them, so a stream that
    // did not remember _bufsiz would overrun the array it was given.  The buffer is
    // also not malloc'd, so an implementation that left _IOMYBUF set would have
    // fclose() free static storage.
    //
    f = fopen(FNAME, "w");
    setbuffer(f, small, sizeof small);
    fputs("0123456789abcdefghij", f);
    fclose(f);
    readback(buf, sizeof buf);
    eqs("setbuffer sized", buf, "0123456789abcdefghij");

    // A null buffer -- or no room for one -- is unbuffered, where setvbuf fails the call.
    f = fopen(FNAME, "w");
    setbuffer(f, NULL, 0);
    fputs("xy\n", f);
    fclose(f);
    readback(buf, sizeof buf);
    eqs("setbuffer NULL", buf, "xy\n");

    // setlinebuf takes the mode with no buffer of its own: _flsbuf finds one.
    f = fopen(FNAME, "w");
    ok("setlinebuf", setlinebuf(f) == 0);
    fputs("ab\ncd\n", f);
    fclose(f);
    readback(buf, sizeof buf);
    eqs("setlinebuf", buf, "ab\ncd\n");
}

//
// open_memstream: a sink with no descriptor behind it that grows to hold whatever
// is written.  What is worth proving rather than transcribing:
//
//   THE BUFFER OUTLIVES fclose.  That is the whole point of the interface, and here
//   it is what fclose() does NOT do -- no close(), no free() -- that makes it true.
//
//   GROWTH IS CHECKED BYTE BY BYTE, with a period-10 pattern over 500 characters, so
//   that a byte lost or duplicated where a realloc boundary fell shows up as a wrong
//   character rather than as a plausible one.  48 bytes is the starting capacity, so
//   this crosses four doublings.
//
//   THE SIDE TABLE is exercised by two streams open at once and written alternately;
//   an implementation that kept one pair of pointers would publish both into one.
//
//   THE SLOT IS RELEASED CLEANLY.  A memory stream carries _IOSTRG, so fclose() takes
//   the arm that neither closes a descriptor nor frees the base -- and if it also
//   left _base pointing at the buffer just handed to the caller, the fopen() below
//   would inherit it (both searches scan from _iob[0], so it takes back the very slot
//   just released), drain the memory stream's stale bytes into the file ahead of its
//   own, and go on writing into storage the caller has since freed.  Nothing else in
//   this program can catch that.
//
static void memstream(void)
{
    FILE *m, *m2, *f;
    char *b, *b2;
    size_t n, n2;
    char buf[80];
    int i, bad;

    // Empty and untouched: a real buffer, a zero length and a NUL, per POSIX.
    m = open_memstream(&b, &n);
    ok("open_memstream", m != NULL);
    if (m == NULL)
        return;
    ok("empty buffer", b != NULL);
    eq("empty length", (long)n, 0);
    ok("empty terminated", b[0] == '\0');
    ok("fclose empty", fclose(m) == 0);
    eq("empty length after close", (long)n, 0);
    free(b);

    //
    // Every writing routine there is, into one stream.  They all funnel through the
    // putc macro, which is the claim being tested: the stream is held at _cnt == 0,
    // so each of these bytes arrives in _flsbuf one at a time.
    //
    m = open_memstream(&b, &n);
    fputs("first", m);
    fprintf(m, " %s %d", "second", 22);
    fwrite(" third", 1, 6, m);
    putc('!', m);
    eq("ftell on memstream", ftell(m), 22);
    ok("fflush publishes", fflush(m) == 0);
    eq("flushed length", (long)n, 22);
    eqs("flushed text", b, "first second 22 third!");
    ok("no error", ferror(m) == 0);
    ok("fclose written", fclose(m) == 0);
    eq("length after close", (long)n, 22);
    eqs("text after close", b, "first second 22 third!");
    free(b);

    // An embedded NUL is data, and is not what terminates the buffer.
    m = open_memstream(&b, &n);
    fwrite("a\0b", 1, 3, m);
    fclose(m);
    eq("embedded NUL length", (long)n, 3);
    ok("embedded NUL kept", b[0] == 'a' && b[1] == '\0' && b[2] == 'b' && b[3] == '\0');
    free(b);

    // Growth across four doublings.
    m = open_memstream(&b, &n);
    for (i = 0; i < 500; i++)
        putc('0' + i % 10, m);
    ok("fclose grown", fclose(m) == 0);
    eq("grown length", (long)n, 500);
    bad = 0;
    for (i = 0; i < 500; i++)
        if (b[i] != '0' + i % 10)
            bad++;
    eq("grown bytes wrong", bad, 0);
    ok("grown terminated", b[500] == '\0');
    free(b);

    // Two at once, interleaved, both growing.
    m  = open_memstream(&b, &n);
    m2 = open_memstream(&b2, &n2);
    for (i = 0; i < 200; i++) {
        putc('a', m);
        putc('b', m2);
        putc('B', m2);
    }
    fclose(m);
    fclose(m2);
    eq("first of two", (long)n, 200);
    eq("second of two", (long)n2, 400);
    bad = 0;
    for (i = 0; i < 200; i++)
        if (b[i] != 'a')
            bad++;
    for (i = 0; i < 400; i++)
        if (b2[i] != (i % 2 == 0 ? 'b' : 'B'))
            bad++;
    eq("two streams crossed", bad, 0);
    free(b);
    free(b2);

    // The slot must come back clean -- see the head comment.
    m = open_memstream(&b, &n);
    fputs("stale", m);
    fclose(m);
    f = fopen(FNAME, "w");
    fputs("zz\n", f);
    fclose(f);
    readback(buf, sizeof buf);
    eqs("slot reused cleanly", buf, "zz\n");
    free(b);
}

//
// fread/fwrite in bulk.  They move a WORD at a time when the caller's buffer and
// the stream's cursor are BOTH on a word boundary (lib/libc/stdio/fread.c), and
// fall back to getc/putc otherwise.  What is worth proving rather than merely
// transcribing:
//
//   ALL THIRTY-SIX COMBINATIONS of the two cursors' byte offsets.  The two are
//   moved independently -- k bytes of filler ahead of the run shift the STREAM's,
//   wbuf+j / rbuf+j shift the CALLER's -- because a word path that fired when only
//   one of them was aligned would corrupt thirty of the thirty-six and leave the
//   diagonal, which is what a naive test sweeps, looking healthy.
//
//   A RUN THAT CROSSES A REFILL.  BUFSIZ is 3072 bytes == 512 words, so a longer
//   run empties the buffer mid-transfer and the word that straddles the boundary
//   has to go back through getc.
//
//   THE MODES THAT MUST DECLINE IT.  A line-buffered or unbuffered stream is held
//   at _cnt == 0 by design, so it can never take the word path -- and an
//   unbuffered one gets _base = &smallbuf[fileno], which is not word-aligned at
//   all.  The bytes must still be the same bytes.
//
//   THE ITEM ACCOUNTING.  The transfer is flattened to size*count bytes inside, so
//   a short read has to divide back to COMPLETE items: ten bytes read as four
//   three-byte items is three, not four and not ten, and the tenth byte is
//   consumed without being counted.
//
#define BULKN   60   // the alignment matrix, run thirty-six times: short on purpose
#define BULKBIG 4000 // longer than BUFSIZ, so a run crosses a refill

static char wbuf[BULKBIG + 8];
static char rbuf[BULKBIG + 8];
static char bulkbuf[BUFSIZ];

// A pattern with no period a lost or duplicated byte could hide in.
static int bulkbyte(int i)
{
    return (i * 7 + 13) & 0377;
}

//
// Push n bytes through the scratch file at stream offset k and caller offset j.
// Answers 0 on success, -1 on a short transfer, else the 1-based index of the
// first byte that came back wrong.
//
static int bulkpass(int k, int j, int n, int mode)
{
    FILE *f;
    int i;

    for (i = 0; i < n; i++) {
        wbuf[j + i] = bulkbyte(i);
        rbuf[j + i] = 0;
    }

    f = fopen(FNAME, "w");
    if (f == NULL)
        return -1;
    if (mode != _IOFBF)
        setvbuf(f, mode == _IONBF ? NULL : bulkbuf, mode, BUFSIZ);
    for (i = 0; i < k; i++)
        putc('#', f);
    if (fwrite(wbuf + j, 1, n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0)
        return -1;

    f = fopen(FNAME, "r");
    if (f == NULL)
        return -1;
    if (mode != _IOFBF)
        setvbuf(f, mode == _IONBF ? NULL : bulkbuf, mode, BUFSIZ);
    for (i = 0; i < k; i++)
        getc(f);
    if (fread(rbuf + j, 1, n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    fclose(f);

    for (i = 0; i < n; i++)
        if (rbuf[j + i] != (char)bulkbyte(i))
            return i + 1;
    return 0;
}

static void bulkio(void)
{
    FILE *f;
    int j, k;

    for (k = 0; k < 6; k++) {
        printf("bulk stream+%d, caller+0..5:", k);
        for (j = 0; j < 6; j++) {
            int rc = bulkpass(k, j, BULKN, _IOFBF);

            printf(" %s", rc == 0 ? "ok" : "FAIL");
            if (rc != 0)
                errors++;
        }
        putchar('\n');
    }

    ok("bulk across a refill, aligned", bulkpass(0, 0, BULKBIG, _IOFBF) == 0);
    ok("bulk across a refill, skewed", bulkpass(1, 4, BULKBIG, _IOFBF) == 0);
    ok("bulk line buffered", bulkpass(0, 0, BULKN, _IOLBF) == 0);
    ok("bulk unbuffered", bulkpass(0, 0, BULKN, _IONBF) == 0);

    f = fopen(FNAME, "w");
    fwrite("0123456789", 1, 10, f);
    fclose(f);

    f = fopen(FNAME, "r");
    eq("fread short items", (long)fread(rbuf, 3, 4, f), 3);
    rbuf[10] = '\0';
    eqs("fread short data", rbuf, "0123456789");
    eq("fread past the end", (long)fread(rbuf, 1, 1, f), 0);
    fclose(f);

    f = fopen(FNAME, "r");
    eq("fread exact items", (long)fread(rbuf, 5, 2, f), 2);
    fclose(f);

    f = fopen(FNAME, "r");
    eq("fread zero size", (long)fread(rbuf, 0, 4, f), 0);
    eq("fread zero count", (long)fread(rbuf, 4, 0, f), 0);
    fclose(f);
}

// remove, rename, tmpnam and tmpfile.
static void names(void)
{
    FILE *f;
    char n1[L_tmpnam], n2[L_tmpnam];
    char buf[80];
    int n;

    ok("rename", rename(FNAME, GNAME) == 0);
    ok("old name gone", fopen(FNAME, "r") == NULL);
    f = fopen(GNAME, "r");
    ok("new name there", f != NULL);
    fclose(f);
    ok("remove", remove(GNAME) == 0);
    ok("removed", fopen(GNAME, "r") == NULL);

    tmpnam(n1);
    tmpnam(n2);
    ok("tmpnam differs", strcmp(n1, n2) != 0);
    ok("tmpnam fits L_tmpnam", (int)strlen(n1) < L_tmpnam);

    f = tmpfile();
    ok("tmpfile", f != NULL);
    if (f != NULL) {
        fputs("gone when closed\n", f);
        rewind(f);
        n      = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        eqs("tmpfile round trip", buf, "gone when closed\n");
        fclose(f);
    }
}

int main(void)
{
    writefile();
    readfile();
    reopen();
    buffering();
    bulkio();
    memstream();
    names();

    printf("%d error(s)\n", errors);
    return errors != 0;
}
