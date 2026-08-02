// The disk-backed gap buffer, by Dave W Plummer.  See buffer.h for the layout and
// README.md for what this machine changed about it.
//
// THREE THINGS UPSTREAM USED ARE NOT HERE, and each was answered by deleting
// something rather than by finding a substitute:
//
//   - ftruncate(2) does not exist in this kernel.  It does not have to: nothing
//     reads the scratch files' PHYSICAL length.  buf_get() bounds every access by
//     nleft/nright, the copy routines are handed explicit lengths, and every write
//     lands at the logical end and grows.  So the files simply never shrink, and
//     each is bounded by the high-water mark of its own half -- at most twice the
//     document between them.  (novi.c's cut buffer is the one place where dropping
//     the truncate is NOT free; E.cutlen is there for that reason.)
//
//   - mkstemp(3) does not exist either; mktemp(3) does.  scratch() is the tree's
//     idiom, cmd/ed/ed.c's: mktemp, creat, reopen for reading and writing, unlink.
//     The template must be a WRITABLE ARRAY -- mktemp writes through it -- and each
//     caller must pass a DIFFERENT one: mktemp only avoids names access(2) can see,
//     and these files are unlinked the moment they are open, so one template would
//     hand back one name every time.
//
//   - rename(2) does not exist, and lib/libc/stdio/rename.c's link()+unlink() stand-in
//     refuses a destination that already exists -- which is every save but the first.
//     buf_save() therefore does not write a temp file at all: creat(2) on an existing
//     file truncates it IN PLACE and ignores the mode argument, so the inode, the
//     owner, the permissions and every hard link survive untouched.  That is exactly
//     what the stat/fchmod pair upstream carries existed to fake.  The cost is in
//     novi.1's BUGS: a write that fails half way leaves the file truncated, which is
//     ed(1)'s `w' on this system too.
//
// AND ONE THING THIS MACHINE ADDED.  Upstream migrates the gap ONE BYTE AT A TIME,
// four syscalls a byte; one PgDn is nineteen move_vertical()s, about 1,500 bytes,
// about 6,000 traps.  migrate() moves BUF_MOVE bytes per iteration instead, so the
// same PgDn costs eight.  The two transfer buffers are static rather than automatic
// on purpose: the 4,096-word stack at 070000 is the one size ceiling nothing checks,
// and in bss rootfs_novi_size weighs them.
#include "buffer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

// How much of the gap migrate() moves per iteration.  Deliberately not BUF_CACHE:
// that one sizes a read cache inside every struct buffer, this one two arrays there
// is exactly one pair of.
#define BUF_MOVE 1024

static char xfer[BUF_MOVE];
static char xrev[BUF_MOVE];

// dst[i] = src[n-1-i].  The one operation the whole design turns on -- the right
// half is stored reversed -- and it happens in three places: loading a file,
// migrating the gap, and streaming the right half back out.
static void revblock(char *dst, char *src, int n)
{
    int i;

    for (i = 0; i < n; ++i)
        dst[i] = src[n - 1 - i];
}

// An open, unlinked, read/write scratch file.  `pat' must be a writable array whose
// tail is X's; see the header comment for why every caller needs its own.
static int scratch(char *pat)
{
    int fd;

    (void)mktemp(pat);
    fd = creat(pat, 0600);
    if (fd < 0)
        return -1;
    (void)close(fd);
    fd = open(pat, O_RDWR);
    (void)unlink(pat);
    return fd;
}

static void uncache(struct buffer *b)
{
    b->cachefd  = -1;
    b->cachelen = 0;
}

// Take the last byte off a half.  No ftruncate: *len is the authority on where the
// half ends, and the byte just dropped is left in the file for the next write to
// overwrite.
static int popone(int fd, off_t *len)
{
    unsigned char c;

    if (*len == 0)
        return -1;
    if (lseek(fd, *len - 1, SEEK_SET) < 0)
        return -1;
    if (read(fd, (char *)&c, 1) != 1)
        return -1;
    --*len;
    return (int)c;
}

// Move `count' bytes across the gap: from left to right when toright, otherwise the
// other way.  The reversal is the same in both directions, and it is worth showing
// why, because getting it backwards is silent and shows up as garbled text a page
// away rather than at the cursor.
//
// Moving n bytes leftward, the right half must hold document[nleft+nright-1-j] at
// physical j for the new nleft/nright as well as the old -- the formula is unchanged,
// so entries 0..nright-1 stay put, and entries nright..nright+n-1 want
// document[nleft-1] first and document[nleft-n] last.  The block read off the left
// half's tail runs document[nleft-n] first.  Reversed.  Rightward is the mirror
// image and comes out the same way.
static int migrate(struct buffer *b, off_t count, int toright)
{
    int src, dst;
    off_t *nsrc, *ndst;
    int n;

    if (toright) {
        src  = b->left;
        nsrc = &b->nleft;
        dst  = b->right;
        ndst = &b->nright;
    } else {
        src  = b->right;
        nsrc = &b->nright;
        dst  = b->left;
        ndst = &b->nleft;
    }
    if (count > *nsrc)
        return -1;
    while (count > 0) {
        n = count > BUF_MOVE ? BUF_MOVE : (int)count;
        if (lseek(src, *nsrc - n, SEEK_SET) < 0 || read(src, xfer, n) != n)
            return -1;
        revblock(xrev, xfer, n);
        if (lseek(dst, *ndst, SEEK_SET) < 0 || write(dst, xrev, n) != n)
            return -1;
        *nsrc -= n;
        *ndst += n;
        count -= n;
    }
    uncache(b);
    return 0;
}

int buf_open(struct buffer *b, char *name)
{
    // Two templates, not one, and arrays rather than literals: see the header
    // comment.  Automatic, so a second buf_open() gets its X's back.
    char lpat[] = "/tmp/noviLXXXXX";
    char rpat[] = "/tmp/noviRXXXXX";
    int in;
    int n;
    off_t left;
    off_t end;

    b->left    = scratch(lpat);
    b->right   = scratch(rpat);
    b->nleft   = 0;
    b->nright  = 0;
    b->changed = 0;
    b->version = 0;
    uncache(b);
    if (b->left < 0 || b->right < 0)
        return -1;
    if (name == (char *)0 || *name == '\0')
        return 0;
    in = open(name, O_RDONLY);
    if (in < 0) {
        // A name that does not exist yet is a new file, not an error.
        if (errno == ENOENT)
            return 0;
        return -1;
    }
    end = lseek(in, (off_t)0, SEEK_END);
    if (end < 0) {
        (void)close(in);
        return -1;
    }
    // Back to front, a block at a time, each block reversed: that IS the right
    // half's storage order, so the whole file lands with the cursor at 0.
    while (end > 0) {
        n    = end > BUF_MOVE ? BUF_MOVE : (int)end;
        left = end - n;
        if (lseek(in, left, SEEK_SET) < 0 || read(in, xfer, n) != n) {
            (void)close(in);
            return -1;
        }
        revblock(xrev, xfer, n);
        if (write(b->right, xrev, n) != n) {
            (void)close(in);
            return -1;
        }
        b->nright += n;
        end = left;
    }
    (void)close(in);
    return 0;
}

void buf_close(struct buffer *b)
{
    if (b->left >= 0)
        (void)close(b->left);
    if (b->right >= 0)
        (void)close(b->right);
    b->left = b->right = -1;
}

off_t buf_size(struct buffer *b)
{
    return b->nleft + b->nright;
}

off_t buf_pos(struct buffer *b)
{
    return b->nleft;
}

int buf_get(struct buffer *b, off_t pos)
{
    int fd;
    off_t physical;
    off_t base;
    int n;

    if (pos < 0 || pos >= buf_size(b))
        return -1;
    if (pos < b->nleft) {
        fd       = b->left;
        physical = pos;
    } else {
        fd       = b->right;
        physical = b->nright - 1 - (pos - b->nleft);
    }
    base = physical - physical % BUF_CACHE;
    if (b->cachefd != fd || physical < b->cachebase || physical >= b->cachebase + b->cachelen) {
        if (lseek(fd, base, SEEK_SET) < 0)
            return -1;
        n = read(fd, (char *)b->cache, BUF_CACHE);
        if (n <= 0)
            return -1;
        b->cachefd   = fd;
        b->cachebase = base;
        b->cachelen  = n;
    }
    return b->cache[(int)(physical - b->cachebase)];
}

int buf_insert(struct buffer *b, int ch)
{
    unsigned char c;

    c = (unsigned char)ch;
    if (lseek(b->left, b->nleft, SEEK_SET) < 0 || write(b->left, (char *)&c, 1) != 1)
        return -1;
    ++b->nleft;
    b->changed = 1;
    ++b->version;
    uncache(b);
    return 0;
}

// Insert n bytes at the cursor in one write.  do_read() and do_uncut() did this a
// byte at a time upstream, two syscalls each.
int buf_insert_block(struct buffer *b, char *s, int n)
{
    if (n <= 0)
        return 0;
    if (lseek(b->left, b->nleft, SEEK_SET) < 0 || write(b->left, s, n) != n)
        return -1;
    b->nleft += n;
    b->changed = 1;
    ++b->version;
    uncache(b);
    return 0;
}

int buf_backspace(struct buffer *b)
{
    int ch;

    ch = popone(b->left, &b->nleft);
    if (ch < 0)
        return -1;
    b->changed = 1;
    ++b->version;
    uncache(b);
    return ch;
}

int buf_delete(struct buffer *b)
{
    int ch;

    ch = popone(b->right, &b->nright);
    if (ch < 0)
        return -1;
    b->changed = 1;
    ++b->version;
    uncache(b);
    return ch;
}

int buf_left(struct buffer *b)
{
    if (b->nleft == 0)
        return -1;
    return migrate(b, (off_t)1, 1);
}

int buf_right(struct buffer *b)
{
    if (b->nright == 0)
        return -1;
    return migrate(b, (off_t)1, 0);
}

int buf_seek(struct buffer *b, off_t pos)
{
    if (pos < 0 || pos > buf_size(b))
        return -1;
    if (pos < b->nleft)
        return migrate(b, b->nleft - pos, 1);
    if (pos > b->nleft)
        return migrate(b, pos - b->nleft, 0);
    return 0;
}

static int copy_forward(int out, int in, off_t len)
{
    int want;
    int n;

    if (lseek(in, (off_t)0, SEEK_SET) < 0)
        return -1;
    while (len > 0) {
        want = len > BUF_MOVE ? BUF_MOVE : (int)len;
        n    = read(in, xfer, want);
        if (n != want || write(out, xfer, n) != n)
            return -1;
        len -= n;
    }
    return 0;
}

static int copy_reverse(int out, int in, off_t len)
{
    off_t start;
    int want;
    int n;

    while (len > 0) {
        want  = len > BUF_MOVE ? BUF_MOVE : (int)len;
        start = len - want;
        if (lseek(in, start, SEEK_SET) < 0)
            return -1;
        n = read(in, xfer, want);
        if (n != want)
            return -1;
        revblock(xrev, xfer, n);
        if (write(out, xrev, n) != n)
            return -1;
        len -= n;
    }
    return 0;
}

// Write the document out.  creat(2) rather than a temp file and a rename: see the
// header comment for why that is the better answer here and not merely the only one.
int buf_save(struct buffer *b, char *name)
{
    int out;
    int ok;

    out = creat(name, 0666);
    if (out < 0)
        return -1;
    ok = copy_forward(out, b->left, b->nleft);
    if (ok == 0)
        ok = copy_reverse(out, b->right, b->nright);
    if (close(out) < 0)
        ok = -1;
    if (ok == 0)
        b->changed = 0;
    return ok;
}
