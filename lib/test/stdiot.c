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
//   an _IOSTRG stream never touches a descriptor, which is why sscanf works on a
//   FILE with _file == -1.
//
// The scratch file is created here and removed at the end, so a run leaves nothing
// behind and the .expected file stays host-independent.
//
#include <stdio.h>
#include <string.h>

// v7 has no <unistd.h> and include/ has none either; a caller declares its own.
int open(const char *path, int mode);

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

    fd = open(FNAME, 0);
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

//
// The same six characters through each buffering mode.  Unbuffered spends a write()
// per byte, line buffered flushes on the newline, fully buffered flushes at fclose;
// all three must leave the same file.
//
static void buffering(void)
{
    static char mybuf[BUFSIZ];
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

        f      = fopen(FNAME, "r");
        n      = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        fclose(f);
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
    f      = fopen(FNAME, "r");
    n      = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    eqs("setbuf NULL", buf, "xy\n");
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
    names();

    printf("%d error(s)\n", errors);
    return errors != 0;
}
