// The disk-backed gap buffer, end to end.  buffer.c touches no terminal, so this is
// the three quarters of novi that DOES have a b6sim half, and it is where the risk of
// the port lives: the block-wise migrate(), the reversal it turns on, and the removed
// ftruncate(2).
//
// THE SEEK CASES ARE NOT DECORATION.  A reversal that comes out backwards is silent:
// the bytes at the cursor are right and the damage is a block away, so nothing an
// interactive smoke test does would find it.  Every seek below therefore crosses
// several BUF_MOVE blocks, in both directions, and every byte is verified afterwards.
//
// /tmp IS THE BUILD MACHINE'S under b6sim, as cmd/ed's cases' /tmp is (cmd/README.md
// SS9: ask whose it is).  Both files are created with mktemp names and unlinked again,
// and nothing outside them is touched.
//
// TEST_SIZE is 20,000 rather than upstream's 200,000: it is twenty times BUF_MOVE, so
// every block path is exercised several times over, and the case stays under a second.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "buffer.h"

#define TEST_SIZE 20000

static int value_at(int i)
{
    return i % 251 + 1;
}

static void fail(char *s)
{
    (void)fprintf(stderr, "test_buffer: %s\n", s);
    exit(1);
}

// Every byte of the document, against what it ought to be.  `what' names the step
// being checked, so a failure says which seek broke it.
static void verify(struct buffer *b, char *what)
{
    int i;

    if (buf_size(b) != TEST_SIZE) {
        (void)fprintf(stderr, "test_buffer: %s: size %d\n", what, (int)buf_size(b));
        fail("wrong size");
    }
    for (i = 0; i < TEST_SIZE; ++i) {
        if (buf_get(b, (off_t)i) != value_at(i)) {
            (void)fprintf(stderr, "test_buffer: %s: byte %d is %d, want %d\n", what, i,
                          buf_get(b, (off_t)i), value_at(i));
            fail("contents");
        }
    }
}

static void seek_and_verify(struct buffer *b, int pos, char *what)
{
    if (buf_seek(b, (off_t)pos) < 0)
        fail(what);
    if (buf_pos(b) != pos)
        fail("seek landed elsewhere");
    verify(b, what);
    if (b->changed)
        fail("seek changed the document");
}

int main(void)
{
    char source[] = "/tmp/noviSXXXXX";
    char output[] = "/tmp/noviOXXXXX";
    unsigned char block[256];
    struct buffer b;
    int made;
    int i;
    int fd;
    int n;
    int ch;

    // ---- a source file of known contents ----
    (void)mktemp(source);
    fd = creat(source, 0600);
    if (fd < 0)
        fail("creat source");
    made = 0;
    while (made < TEST_SIZE) {
        n = TEST_SIZE - made > (int)sizeof block ? (int)sizeof block : TEST_SIZE - made;
        for (i = 0; i < n; ++i)
            block[i] = value_at(made + i);
        if (write(fd, (char *)block, n) != n)
            fail("write source");
        made += n;
    }
    (void)close(fd);

    // ---- loading it puts the whole document in the right half, reversed ----
    b.left = b.right = -1;
    if (buf_open(&b, source) < 0)
        fail("buf_open");
    if (buf_pos(&b) != 0)
        fail("initial position");
    verify(&b, "after buf_open");

    // ---- seeks across many BUF_MOVE blocks, both directions, and the two ends.
    // Each migrates thousands of bytes; a reversal that came out backwards shows up
    // in verify() and nowhere else.
    seek_and_verify(&b, 7777, "seek forward");
    seek_and_verify(&b, 1234, "seek back");
    seek_and_verify(&b, TEST_SIZE, "seek to end");
    seek_and_verify(&b, 0, "seek to start");
    seek_and_verify(&b, TEST_SIZE / 2, "seek to middle");
    seek_and_verify(&b, TEST_SIZE - 1, "seek to last byte");
    seek_and_verify(&b, 1, "seek to first byte");

    // ---- one-byte motion is the same path with count 1 ----
    if (buf_left(&b) < 0 || buf_pos(&b) != 0)
        fail("buf_left");
    if (buf_left(&b) >= 0)
        fail("buf_left past the start");
    if (buf_right(&b) < 0 || buf_pos(&b) != 1)
        fail("buf_right");
    verify(&b, "after single steps");

    // ---- edits ----
    if (buf_seek(&b, (off_t)1000) < 0 || b.changed)
        fail("seek changed document");
    if (buf_insert(&b, 'Z') < 0)
        fail("insert");
    if (!b.changed)
        fail("insert did not mark the document changed");
    if (buf_delete(&b) != value_at(1000))
        fail("delete");
    if (buf_seek(&b, buf_size(&b)) < 0 || buf_insert(&b, 'Q') < 0)
        fail("append");

    // ---- buf_insert_block: the same insertion, in one write ----
    if (buf_seek(&b, (off_t)500) < 0)
        fail("seek before block insert");
    if (buf_insert_block(&b, "ABC", 3) < 0)
        fail("insert block");
    if (buf_pos(&b) != 503 || buf_size(&b) != TEST_SIZE + 4)
        fail("block insert position or size");
    for (i = 0; i < 3; ++i) {
        if (buf_backspace(&b) != "ABC"[2 - i])
            fail("backspace over the block");
    }
    if (buf_size(&b) != TEST_SIZE + 1)
        fail("size after backspace");

    // ---- save, twice: the second is OVER AN EXISTING FILE, which is what
    // creat(2)-in-place is here for and what a link()+unlink() rename could not do.
    if (buf_save(&b, output) < 0 || b.changed)
        fail("save");
    if (buf_insert(&b, '!') < 0)
        fail("insert before the second save");
    if (buf_backspace(&b) != '!')
        fail("backspace before the second save");
    if (buf_save(&b, output) < 0 || b.changed)
        fail("save over an existing file");
    buf_close(&b);

    // ---- and the saved bytes are the document ----
    fd = open(output, O_RDONLY);
    if (fd < 0)
        fail("open output");
    for (i = 0; i < TEST_SIZE + 1; ++i) {
        if (read(fd, (char *)&block[0], 1) != 1)
            fail("short output");
        ch = i == 1000 ? 'Z' : i == TEST_SIZE ? 'Q' : value_at(i);
        if (block[0] != ch) {
            (void)fprintf(stderr, "test_buffer: output byte %d is %d, want %d\n", i, block[0], ch);
            fail("saved contents");
        }
    }
    if (read(fd, (char *)&block[0], 1) != 0)
        fail("long output");
    (void)close(fd);
    (void)unlink(source);
    (void)unlink(output);
    (void)printf("buffer tests passed\n");
    return 0;
}
