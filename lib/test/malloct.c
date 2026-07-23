/*
 * malloct -- the v7 storage allocator on a word-addressed machine.
 *
 * The one thing that had to change in the port is where the BUSY flag lives: v7 keeps
 * it in bit 0 of a block's link, which here is a significant bit of a WORD address, so
 * it moved to bit 16, one past the top of the address space (lib/libc/gen/malloc.c).
 * A flag in the wrong place does not crash -- it hands back a pointer one word off, or
 * loses a block to the free list -- so the checks below are about identity rather than
 * about surviving: which block comes back, and whether its neighbours merged.
 *
 * Nothing host-dependent may reach the output, and an address is the most host-dependent
 * thing here: the heap starts at `end', which moves whenever the program does.  So no
 * pointer is ever printed; every check compares one pointer against another.
 *
 * Named malloct and not malloc so the program does not collide with the libc member it
 * exercises, exactly as sbrkt does.  It carries its own output routines: stdio is phase 4.
 */
#include <stdlib.h>
#include <string.h>

int write(int fd, char *buf, int n);
void cfree(void *p, size_t num, size_t size);

/* One string to the standard output, without stdio (phase 4). */
static void put(char *s)
{
    write(1, s, strlen(s));
}

static void putdigit(int d)
{
    char *p = "0123456789";

    while (d-- > 0)
        p++;
    write(1, p, 1);
}

static void putnum(int v)
{
    if (v < 0) {
        put("-");
        v = -v;
    }
    if (v >= 10)
        putnum(v / 10);
    putdigit(v % 10);
}

static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

static void shownum(char *what, int v)
{
    put(what);
    put(" ");
    putnum(v);
    put("\n");
}

/* A signature a block can be checked against later: byte i of block `seed'. */
static void fill(char *p, int n, int seed)
{
    int i;

    for (i = 0; i < n; i++)
        p[i] = (char)((seed + i) & 0177);
}

static int intact(char *p, int n, int seed)
{
    int i;

    for (i = 0; i < n; i++)
        if (p[i] != (char)((seed + i) & 0177))
            return 0;
    return 1;
}

/*
 * The churn: 200 blocks of an unpredictable size, each carrying its own signature, then
 * freed in an order that is not the order they were allocated in.  rand() is the v7
 * recurrence with no srand(), so the sizes are the same on every run; 7 is coprime with
 * 200, so stepping by 7 visits every block exactly once.
 */
#define NCHURN 200

static char *blk[NCHURN];
static int len[NCHURN];

int main(int argc, char **argv, char **envp)
{
    char *p, *a, *b, *c, *z;
    char *x, *y, *guard, *big;
    char *r;
    int *ci;
    int i, k, n, bad;

    /* A block, and every byte of it. */
    p = (char *)malloc(100);
    ok("malloc returns a block", p != NULL);
    fill(p, 100, 1);
    ok("every byte of it round-trips", intact(p, 100, 1));

    /*
     * It begins at byte #0 of a word.  Casting a char * to int * discards the byte
     * offset and casting back rebuilds it as byte #0, so the round trip is the identity
     * exactly for a pointer that was already word-aligned -- the same test qsort makes
     * of its base before it exchanges word-wise.
     */
    ok("the block starts on a word boundary", p == (char *)(int *)p);

    /* Two live blocks are distinct and do not overlap. */
    a = (char *)malloc(50);
    b = (char *)malloc(50);
    fill(a, 50, 2);
    fill(b, 50, 3);
    ok("two live blocks are distinct", a != b && a != NULL && b != NULL);
    ok("neither disturbs the other", intact(a, 50, 2) && intact(b, 50, 3));
    ok("nor the first block", intact(p, 100, 1));

    /* A zero-size request still names a block of its own, and can be freed. */
    z = (char *)malloc(0);
    ok("malloc(0) returns a block", z != NULL && z != a && z != b);
    free(z);

    /*
     * free() leaves the search pointer ON the block it released -- v7's tuning for LIFO
     * allocation -- so the next request of the same size gets that very block back.  It
     * only can if free() cleared the BUSY flag out of the link and left the address
     * alone: a flag cleared into the address would return a block one word off, and a
     * flag not cleared at all would step over it.
     */
    free(b);
    c = (char *)malloc(50);
    ok("a freed block comes straight back", c == b);
    free(c);
    free(a);

    /*
     * Coalescing, and the sharpest check in the file.  Three blocks of eleven words
     * each; the third is never freed, so the search cannot simply walk past into the
     * arena's tail.  Freeing y and then x leaves the search pointer on x with y free
     * behind it, and a request for exactly twenty-two words can be met only if the walk
     * merges the two.  Twenty-two words is malloc(121): the header is one of them.
     */
    x     = (char *)malloc(60);
    y     = (char *)malloc(60);
    guard = (char *)malloc(60);
    free(y);
    free(x);
    big = (char *)malloc(121);
    ok("two freed neighbours coalesce", big == x);
    free(big);
    free(guard);
    free(p);

    /*
     * The churn, which is the only part of this program that makes the heap grow: 200
     * blocks of up to 300 bytes is some 5000 words, and sbrk() is asked for a page at a
     * time.  Every block is verified just before it is freed, so a block handed out
     * twice, or overlapping its neighbour, shows up as a broken signature.
     */
    n   = 0;
    bad = 0;
    for (i = 0; i < NCHURN; i++) {
        len[i] = rand() % 300 + 1;
        blk[i] = (char *)malloc(len[i]);
        if (blk[i] == NULL)
            continue;
        n++;
        fill(blk[i], len[i], i);
    }
    for (i = 0; i < NCHURN; i++)
        if (blk[i] != NULL && !intact(blk[i], len[i], i))
            bad++;
    shownum("churn blocks allocated", n);
    ok("every one of them kept its contents", bad == 0);

    for (k = 0, i = 0; k < NCHURN; k++) {
        i = (i + 7) % NCHURN;
        if (blk[i] != NULL && !intact(blk[i], len[i], i))
            bad++;
        free(blk[i]);
        blk[i] = NULL;
    }
    ok("and kept them until it was freed", bad == 0);

    /* What the churn returned is usable again, in one piece. */
    p = (char *)malloc(20000);
    ok("the arena is reusable after the churn", p != NULL);
    free(p);

    /*
     * Two refusals.  The first is larger than the heap can ever grow -- sbrk() stops
     * below the stack at 070000 -- and the second is larger than the address space,
     * which malloc has to catch before it computes anything: a size_t is 48 bits wide
     * and a word address is 15, so the word count would WRAP and 0600000 bytes would
     * come back as a one-word block.
     */
    ok("a request too large for the heap fails", malloc(190000) == NULL);
    ok("a request larger than the address space fails", malloc(0600000) == NULL);
    p = (char *)malloc(64);
    ok("the allocator still works after a refusal", p != NULL);
    free(p);

    /* calloc: the product, the zeroing, and the overflow v7 did not test for. */
    ci = (int *)calloc(20, 6);
    ok("calloc returns a block", ci != NULL);
    bad = 0;
    for (i = 0; i < 20; i++)
        if (ci[i] != 0)
            bad++;
    ok("calloc zeroed every word of it", bad == 0);
    ok("calloc refuses a product that would wrap", calloc(1099511627776, 1024) == NULL);
    ok("calloc of no elements still returns a block", calloc(0, 100) != NULL);
    cfree(ci, 20, 6);
    ok("cfree releases the block", (int *)calloc(20, 6) == ci);
    free(ci);

    /* realloc: grow, shrink, and the C11 null case v7 would have faulted on. */
    r = (char *)malloc(30);
    fill(r, 30, 9);
    r = (char *)realloc(r, 300);
    ok("realloc grew the block", r != NULL);
    ok("... keeping the old contents", intact(r, 30, 9));
    r = (char *)realloc(r, 12);
    ok("realloc shrank it", r != NULL);
    ok("... keeping the prefix", intact(r, 12, 9));
    free(r);
    r = (char *)realloc(NULL, 24);
    ok("realloc(NULL, n) allocates", r != NULL);
    free(r);

    /*
     * aligned_alloc has nothing to do but check its argument: every block already begins
     * at byte #0 of a word, and there is no alignment above a word to satisfy.
     */
    p = (char *)aligned_alloc(6, 48);
    ok("aligned_alloc(6) returns a word-aligned block", p != NULL && p == (char *)(int *)p);
    free(p);
    ok("aligned_alloc refuses more than a word", aligned_alloc(8, 48) == NULL);

    free(NULL);
    ok("free(NULL) is a no-op", 1);

    put("done\n");
    return 0;
}
