/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The v7 quicksort, unchanged in its algorithm: a three-way partition that keeps the
 * run of elements equal to the pivot in the middle, recurses on the smaller half and
 * loops on the larger, so the recursion depth is logarithmic.
 *
 * TWO BESM-6 NOTES.  Both are about what the machine does to a `char *'; the structure
 * of the file is v7's, down to the file-scope comparison function.
 *
 * The pointer comparisons (`i < lp', `j > hp') and the difference `l - a' are safe
 * even though a fat pointer does NOT sort as a plain word: incrementing a `char *'
 * DECREASES its 3-bit byte offset, which lives above the word address in bits 47-45,
 * so a raw word compare of two pointers into the same word would come out backwards
 * (doc/Besm6_Data_Representation.md).  The compiler knows: it lowers a relational
 * between two fat pointers through b$pdiff, the same helper as `-', and tests the
 * sign of the byte difference.  Nothing here has to work around it.
 *
 * The exchanges go a WORD at a time when they can, which is the one thing this file
 * adds to v7.  A byte-wise exchange of a six-byte element is six read-modify-writes
 * of the same word through the fat-pointer helpers; one `int' assignment moves the
 * whole word.  It is only legal when the element size is a whole number of words AND
 * the base is word-aligned, and alignment is testable in C: casting a `char *' to
 * `int *' discards the byte offset and casting back rebuilds it as byte #0, so the
 * round trip is the identity exactly for a pointer that was already at byte #0.  With
 * es a multiple of NBPW an aligned base makes every element aligned, so the test is
 * made once, here, and not in the swap.
 */
#include <stdlib.h>

#define NBPW 6 /* bytes per word: sizeof(int) */

static int (*qscmp)(const void *, const void *); /* the caller's comparison */
static int qses;                                 /* element size in bytes */
static int qsws; /* ... in words, or 0 when the swap must go byte-wise */

static void qs1(char *a, char *l);
static void qsexc(char *i, char *j);
static void qstexc(char *i, char *j, char *k);

void qsort(void *base, size_t n, size_t es, int (*fc)(const void *, const void *))
{
    char *a = base;

    qscmp = fc;
    qses  = es;
    qsws  = (es % NBPW == 0 && a == (char *)(int *)a) ? es / NBPW : 0;
    qs1(a, a + n * es);
}

static void qs1(char *a, char *l)
{
    char *i, *j;
    int es;
    char *lp, *hp;
    int c;
    int n;

    es = qses;

start:
    if ((n = l - a) <= es)
        return;
    n  = es * (n / (2 * es));
    hp = lp = a + n;
    i       = a;
    j       = l - es;
    for (;;) {
        if (i < lp) {
            if ((c = (*qscmp)(i, lp)) == 0) {
                qsexc(i, lp -= es);
                continue;
            }
            if (c < 0) {
                i += es;
                continue;
            }
        }

    loop:
        if (j > hp) {
            if ((c = (*qscmp)(hp, j)) == 0) {
                qsexc(hp += es, j);
                goto loop;
            }
            if (c > 0) {
                if (i == lp) {
                    qstexc(i, hp += es, j);
                    i = lp += es;
                    goto loop;
                }
                qsexc(i, j);
                j -= es;
                i += es;
                continue;
            }
            j -= es;
            goto loop;
        }

        if (i == lp) {
            if (lp - a >= l - hp) {
                qs1(hp + es, l);
                l = lp;
            } else {
                qs1(a, lp);
                a = hp + es;
            }
            goto start;
        }

        qstexc(j, lp -= es, i);
        j = hp -= es;
    }
}

/*
 * Exchange the elements at i and j.
 */
static void qsexc(char *i, char *j)
{
    char *ri, *rj, c;
    int *wi, *wj, w;
    int n;

    if ((n = qsws) != 0) {
        wi = (int *)i;
        wj = (int *)j;
        do {
            w     = *wi;
            *wi++ = *wj;
            *wj++ = w;
        } while (--n);
        return;
    }
    n  = qses;
    ri = i;
    rj = j;
    do {
        c     = *ri;
        *ri++ = *rj;
        *rj++ = c;
    } while (--n);
}

/*
 * Rotate the elements at i, j and k: i <- k <- j <- i.
 */
static void qstexc(char *i, char *j, char *k)
{
    char *ri, *rj, *rk, c;
    int *wi, *wj, *wk, w;
    int n;

    if ((n = qsws) != 0) {
        wi = (int *)i;
        wj = (int *)j;
        wk = (int *)k;
        do {
            w     = *wi;
            *wi++ = *wk;
            *wk++ = *wj;
            *wj++ = w;
        } while (--n);
        return;
    }
    n  = qses;
    ri = i;
    rj = j;
    rk = k;
    do {
        c     = *ri;
        *ri++ = *rk;
        *rk++ = *rj;
        *rj++ = c;
    } while (--n);
}
