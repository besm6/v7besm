//
// aoutt -- cmd/libaout's word path, on the machine it was written for.
//
// ../test/ is GoogleTest and builds for the HOST, where the `besm6' macro is not
// defined and ../fastio.h compiles to nothing at all.  It stays the authority on
// the on-disk byte layout of the fallback path; this program covers the branch it
// cannot see, where fgetw()/fputw() move a whole 48-bit word to or from the stdio
// buffer with one load or one store because the cursor happens to sit on a word
// boundary.
//
// What is worth proving rather than merely transcribing:
//
//   THE BYTES ON DISK ARE THE SAME BYTES either way.  Every case here checks the
//   file with getc against an independently computed big-endian decomposition, not
//   only by reading it back with fgetw -- an encode bug and a decode bug cancel in
//   a round trip, which is exactly why ../test/word_test.cpp asserts raw bytes.
//
//   ALL SIX CURSOR ALIGNMENTS.  The stream is offset by k stray bytes ahead of the
//   run, so the fast path is legal for k == 0 and must decline for the other five.
//   A guard that tested only `six bytes are buffered' and not `the cursor is at
//   byte #0' passes at k == 0 and corrupts every other column.
//
//   A RUN THAT CROSSES A REFILL.  BUFSIZ is 3072 bytes == 512 words, and the runs
//   below are 700 words, so the buffer empties mid-run and the straddling word has
//   to go back through fgeth.
//
//   THE MODES THAT DECLINE IT.  An unbuffered stream is held at _cnt == 0 in both
//   directions and is given _base = &smallbuf[fd], which is not word-aligned at
//   all.  _IOLBUF is one-sided: it holds a WRITE stream at _cnt == 0, so fputw
//   there always goes through putc, but it means nothing to _filbuf, so the read
//   back does take the word path.  Both halves are checked.
//
//   ungetc, which has no pushback slot of its own -- it steps _ptr back into the
//   buffer, moving the byte-offset field the guard reads.
//
//   THE EOF ANSWER IS UNCHANGED.  fgeth has no EOF check, so fgetw past the end
//   yields all ones; that is pinned here rather than described as "some EOF", so
//   the fast path cannot quietly alter it.
//
// The scratch file is created here and removed at the end, and no absolute path is
// ever printed -- see ../../README.md SS9.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "besm6/b.out.h"

#define FNAME "aoutt.tmp"

#define NW 700 // words per run: more than BUFSIZ/6 == 512, so a run crosses a refill

static int errors;

// A buffer big enough to be handed to setvbuf at any byte offset.
static char altbuf[BUFSIZ + 8];

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
static void equ(const char *what, uword_t got, uword_t want)
{
    if (got == want)
        printf("ok   %s %#x\n", what, got);
    else {
        printf("FAIL %s %#x, want %#x\n", what, got, want);
        errors++;
    }
}

// Every word distinct, every one using all six bytes, none of them a repeated byte.
static uword_t wordat(int i)
{
    return (0x112233445566U + (uword_t)i * 0x010407020503U) & 0xFFFFFFFFFFFFU;
}

//
// One word and one half-word, checked against a literal byte sequence.  This is the
// case that cannot be satisfied by a self-consistent pair of bugs.
//
static void golden(void)
{
    static const int want[] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x00, 0xFE, 0xDC };
    FILE *f;
    int i, bad = 0;

    f = fopen(FNAME, "w");
    if (f == NULL) {
        ok("golden fopen", 0);
        return;
    }
    fputw(0x123456789ABCU, f);
    fputh(0x00FEDCL, f);
    fclose(f);

    f = fopen(FNAME, "r");
    if (f == NULL) {
        ok("golden reopen", 0);
        return;
    }
    for (i = 0; i < 9; i++)
        if (getc(f) != want[i] && bad == 0)
            bad = i + 1;
    eq("golden bytes, first wrong", bad, 0);
    ok("golden length", getc(f) == EOF);
    fclose(f);

    f = fopen(FNAME, "r");
    equ("golden word back", fgetw(f), 0x123456789ABCU);
    equ("golden half back", (uword_t)fgeth(f), 0x00FEDCU);
    fclose(f);
}

//
// Write k stray bytes, then NW words, then a trailing half-word; read the whole
// file back BYTE BY BYTE and compare with the big-endian decomposition.  Answers 0
// on success, else the 1-based byte index that first disagreed, or a negative code.
//
static int bytecheck(int k, int mode, char *ubuf)
{
    FILE *f;
    int i, j;

    f = fopen(FNAME, "w");
    if (f == NULL)
        return -1;
    if (mode != _IOFBF || ubuf != NULL)
        setvbuf(f, ubuf, mode, BUFSIZ);
    for (i = 0; i < k; i++)
        putc('#', f);
    for (i = 0; i < NW; i++)
        fputw(wordat(i), f);
    fputh(0x00ABCDL, f);
    if (fclose(f) != 0)
        return -2;

    f = fopen(FNAME, "r");
    if (f == NULL)
        return -3;
    for (i = 0; i < k; i++)
        if (getc(f) != '#') {
            fclose(f);
            return -4;
        }
    for (i = 0; i < NW; i++) {
        uword_t w = wordat(i);

        for (j = 5; j >= 0; j--)
            if (getc(f) != (int)((w >> (8 * j)) & 0377)) {
                fclose(f);
                return k + i * 6 + (5 - j) + 1;
            }
    }
    if (getc(f) != 0x00 || getc(f) != 0xAB || getc(f) != 0xCD) {
        fclose(f);
        return -5;
    }
    if (getc(f) != EOF) {
        fclose(f);
        return -6;
    }
    fclose(f);
    return 0;
}

//
// The same file, read back through fgetw/fgeth rather than getc.  Answers 0, or the
// 1-based index of the first word that disagreed.
//
static int wordcheck(int k, int mode, char *ubuf)
{
    FILE *f;
    int i;

    f = fopen(FNAME, "r");
    if (f == NULL)
        return -1;
    if (mode != _IOFBF || ubuf != NULL)
        setvbuf(f, ubuf, mode, BUFSIZ);
    for (i = 0; i < k; i++)
        getc(f);
    for (i = 0; i < NW; i++)
        if (fgetw(f) != wordat(i)) {
            fclose(f);
            return i + 1;
        }
    if (fgeth(f) != 0x00ABCDL) {
        fclose(f);
        return -2;
    }
    fclose(f);
    return 0;
}

//
// ungetc steps _ptr back into the buffer, so it moves the very field the guard
// reads.  Pushed back at each of the six offsets, the word after it must still
// come out whole.
//
static void ungetcase(void)
{
    int k, bad = 0;

    for (k = 1; k <= 6; k++) {
        FILE *f;
        int i;

        f = fopen(FNAME, "w");
        if (f == NULL) {
            ok("ungetc fopen", 0);
            return;
        }
        for (i = 0; i < 8; i++)
            fputw(wordat(i), f);
        fclose(f);

        f = fopen(FNAME, "r");
        if (f == NULL) {
            ok("ungetc reopen", 0);
            return;
        }
        for (i = 0; i < k; i++)
            getc(f);
        ungetc('#', f);
        if (getc(f) != '#' && bad == 0)
            bad = k;
        for (i = k; i < 6; i++)
            getc(f);
        if (fgetw(f) != wordat(1) && bad == 0)
            bad = k;
        fclose(f);
    }
    eq("ungetc before fgetw, first bad offset", bad, 0);
}

//
// A file of 6n + 3 bytes: the tail is half a word.  It reads back as a half-word,
// and only the read past it hits EOF.
//
static void eofcase(void)
{
    FILE *f;
    int i;

    f = fopen(FNAME, "w");
    if (f == NULL) {
        ok("eof fopen", 0);
        return;
    }
    for (i = 0; i < 3; i++)
        fputw(wordat(i), f);
    fputh(0x00ABCDL, f);
    fclose(f);

    f = fopen(FNAME, "r");
    if (f == NULL) {
        ok("eof reopen", 0);
        return;
    }
    for (i = 0; i < 3; i++)
        equ("word before the tail", fgetw(f), wordat(i));
    equ("tail half-word", (uword_t)fgeth(f), 0x00ABCDU);
    // fgeth has no EOF check, so three getc's of -1 make a half-word of all ones,
    // and fgetw ORs two of them together.  Pinned, not described.
    equ("fgetw past the end", fgetw(f), 0xFFFFFFFFFFFFU);
    fclose(f);
}

int main(void)
{
    int k;

    golden();

    // The alignment sweep.  k == 0 is the only column where the fast path is legal;
    // the other five must decline it and produce identical bytes anyway.
    for (k = 0; k < 6; k++) {
        char what[40];

        sprintf(what, "stream+%d bytes on disk", k);
        eq(what, bytecheck(k, _IOFBF, NULL), 0);
        sprintf(what, "stream+%d read back as words", k);
        eq(what, wordcheck(k, _IOFBF, NULL), 0);
    }

    // A buffer whose BASE is misaligned: _cnt is large and the count half of the
    // guard passes, so this is the case that tests the pointer half by itself.
    for (k = 0; k < 6; k++) {
        char what[40];

        sprintf(what, "buffer base+%d bytes on disk", k);
        eq(what, bytecheck(0, _IOFBF, altbuf + k), 0);
        sprintf(what, "buffer base+%d read back as words", k);
        eq(what, wordcheck(0, _IOFBF, altbuf + k), 0);
    }

    // Held at _cnt == 0, so the word path can never fire.
    eq("line buffered on disk", bytecheck(0, _IOLBF, altbuf), 0);
    eq("line buffered as words", wordcheck(0, _IOLBF, altbuf), 0);
    eq("unbuffered on disk", bytecheck(0, _IONBF, NULL), 0);
    eq("unbuffered as words", wordcheck(0, _IONBF, NULL), 0);

    ungetcase();
    eofcase();

    remove(FNAME);
    printf("%d error(s)\n", errors);
    return errors != 0;
}
