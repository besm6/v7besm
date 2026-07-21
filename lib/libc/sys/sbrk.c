/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * sbrk(), brk() -- move the program break.
 *
 * C, and not a stub, because the gate speaks a different language than C does.  The
 * `break' syscall takes a WORD ADDRESS: a fat char * and a plain word address arrive at
 * it as the same 15 bits, and the byte offset is simply dropped (sbreak() in
 * kernel/sys1.c, SYS_break in cmd/sim/syscall.cpp).  A mid-word pointer would therefore
 * floor the break to its word, and hand back memory that was never granted.
 *
 * So the conversion is done here instead: `curbrk' is a real char *, kept word-aligned
 * by construction, and it only ever moves by whole words -- btow(incr) * NBPW.  The
 * pointer handed to _break() is then always at offset 0 and the gate's truncation of it
 * is exact.  The two calls share `curbrk' and so must share a file, as they do in v7.
 *
 * FAILURE IS NULL, NOT (char *)-1.  v7's sbrk returned -1, which on this machine would
 * mean fabricating a fat pointer out of an integer -- exactly what lib/README.md's
 * ground rules forbid, since the bit-48 marker and the offset field would be wrong.
 * NULL costs nothing instead: the break sits above the program's bss and can never
 * legitimately be word 0.  Phase 3's malloc tests for NULL.
 *
 * Both are hand-written on top of _break(), which IS a generated stub
 * (sys/syscalls.tbl); the underscore is there because `break' is a C keyword.
 */

#include <sys/param.h>

/*
 * The linker's end-of-bss symbol (b6ld defines it in cmd/ld/ld.c), declared as an array
 * so that its decay produces a genuine fat pointer at offset 0 rather than a cast.
 */
extern char end[];

int _break(char *addr);

/*
 * Seeded on first use rather than by an initializer: a static fat pointer whose value
 * is a linker symbol has to be relocated at load time, which is more than this needs.
 * It is never 0 once seeded, so the test doubles as the "already done" flag.
 */
static char *curbrk;

/*
 * A byte increment as a whole number of words, rounded in the direction that never
 * hands out a byte the break does not cover.
 *
 * Growth rounds UP, which is btow().  Shrinkage has to round toward zero instead --
 * freeing 617 bytes may free 102 words, not 103, or the 618th byte goes back to the
 * kernel while the caller still owns it.  btow() cannot do that job: it is (x + 5) / 6,
 * and C truncates a negative quotient toward zero, so btow(-618) is -102 and an exact
 * multiple of six would come up one word short.  Plain division is already right for
 * the negative case, for exactly the same truncation.
 */
static int words(int incr)
{
    if (incr < 0)
        return incr / NBPW;
    return btow(incr);
}

/*
 * Grow (or shrink) the break by `incr' bytes and return the OLD break -- which is the
 * fat pointer we are already holding, so nothing is fabricated on the way out.
 */
char *sbrk(int incr)
{
    char *old, *new;

    if (curbrk == 0)
        curbrk = end;
    old = curbrk;
    new = old + words(incr) * NBPW;

    /*
     * A char * carries a 15-bit word address, so an increment past the end of the
     * address space does not overflow -- it WRAPS, quietly, and hands back a pointer
     * low enough for the kernel to grant.  An int is 41 bits and holds such an
     * increment easily, so the wrap is invisible until the caller writes through the
     * result.  Catch it by the only symptom it has: growth that did not grow.
     */
    if (incr > 0 && new <= old)
        return 0;

    if (_break(new) < 0)
        return 0;
    curbrk = new;
    return old;
}

/*
 * Set the break to `addr'; 0 on success, -1 on failure.
 *
 * Expressed as an sbrk() of the pointer difference so the word rounding lives in one
 * place: `addr' may point mid-word, and it is the DIFFERENCE that gets rounded up, so
 * curbrk stays word-aligned and every byte of `addr' remains inside the break.
 */
int brk(char *addr)
{
    if (curbrk == 0)
        curbrk = end;
    if (sbrk(addr - curbrk) == 0)
        return -1;
    return 0;
}
