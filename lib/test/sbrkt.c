//
// sbrkt -- the program break, in bytes on top of a gate that speaks words.
//
// `break' takes a WORD address and drops any byte offset, so the whole job of
// sys/sbrk.c is to keep `curbrk' word-aligned and convert byte increments itself.  Two
// mistakes would survive a casual test: handing the gate a mid-word pointer, which
// floors the break and hands out memory that was never granted; and returning the new
// break instead of the old one, which every caller of sbrk() reads as its buffer.
//
// So the test writes to every byte it was given and reads it back.  Under b6sim the
// break is enforced (SYS_break refuses growth reaching 070000 and the loader seeds it
// just past bss), so a region that was not really granted is not really writable.
//
// Named sbrkt and not sbrk so the program does not collide with the libc member it
// exercises.  Like hello.c it declares what it calls and carries its own output.
//

int write(int fd, char *buf, int n);
char *sbrk(int incr);
int brk(char *addr);

// One string to the standard output, without stdio (phase 4).
static void put(char *s)
{
    char *p = s;
    int n   = 0;

    while (*p) {
        p++;
        n++;
    }
    write(1, s, n);
}

// One decimal digit, taken out of a literal: there is no itoa yet (phase 2).
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

int main(int argc, char **argv, char **envp)
{
    char *base, *p, *q;
    int i;

    //
    // sbrk(0) reports the break without moving it, and must agree with itself.  It is
    // also the only way this program can name the heap: fabricating a pointer from an
    // integer is what lib/README.md forbids, and what sbrk() exists to avoid.
    //
    base = sbrk(0);
    ok("sbrk(0) is not null", base != 0);
    ok("sbrk(0) does not move the break", sbrk(0) == base);

    //
    // A growth of one whole word.  The OLD break comes back -- that is the buffer --
    // and the new one is exactly six bytes further on, NBPW being 6.
    //
    p = sbrk(6);
    ok("sbrk(6) returns the old break", p == base);
    ok("sbrk(6) advanced by one word", sbrk(0) == base + 6);

    //
    // A growth that is not a whole number of words.  btow() rounds UP, so the caller
    // gets at least what it asked for and the break stays word-aligned: 7 bytes costs
    // two words, i.e. twelve.  A version that passed the byte count straight to the
    // gate would move the break by one word and leave the seventh byte ungranted.
    //
    p = sbrk(7);
    ok("sbrk(7) returns the old break", p == base + 6);
    ok("sbrk(7) rounded up to two words", sbrk(0) == base + 6 + 12);

    //
    // Every byte of a larger region must really be there.  600 bytes crosses the page
    // the kernel rounds to, so a break that was floored rather than rounded up would
    // fault here rather than merely miscount.
    //
    q = sbrk(600);
    ok("sbrk(600) returns the old break", q == sbrk(0) - 600);
    for (i = 0; i < 600; i++)
        q[i] = (char)(i & 0177);
    for (i = 0; i < 600; i++)
        if (q[i] != (char)(i & 0177)) {
            put("FAIL heap byte ");
            putnum(i);
            put("\n");
            break;
        }
    if (i == 600)
        ok("the whole region round-trips", 1);

    //
    // brk() puts it back.  It is an sbrk() of the pointer difference, so the rounding
    // lives in one place -- and shrinking back to a word-aligned address is exact.
    //
    ok("brk back to the base", brk(base) == 0);
    ok("the break is back where it started", sbrk(0) == base);

    //
    // An increment so large that the word address wraps its 15 bits.  An int holds
    // it easily -- ints are 41 bits -- so nothing overflows on the way in, and the
    // resulting pointer comes out LOW enough for the kernel to grant.  sbrk has to
    // notice that growth did not grow.
    //
    ok("an sbrk that wraps the address space fails", sbrk(0100000 * 6) == 0);
    ok("a failed sbrk does not move the break", sbrk(0) == base);

    //
    // And the boundary malloc will really meet: the heap may not reach the stack at
    // 070000, so growing a page at a time must eventually be refused -- with the
    // break left exactly where the last success put it.  32 pages is the whole
    // address space, so the loop cannot run away.
    //
    for (i = 0; i < 32; i++)
        if (sbrk(1024 * 6) == 0)
            break;
    ok("sbrk stops before the stack", i < 32);
    p = sbrk(0);
    ok("the refused sbrk left the break alone", sbrk(0) == p);
    ok("brk back to the base again", brk(base) == 0);
    ok("the break is back where it started again", sbrk(0) == base);

    put("done\n");
    return 0;
}
