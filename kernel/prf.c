/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include <stdarg.h>
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/seg.h"
#include "sys/buf.h"
#include "sys/conf.h"
// clang-format on

/*
 * In case console is off,
 * panicstr contains argument to last
 * call to panic.
 */
char *panicstr;

void printn(unsigned long n, int b);

/*
 * Scaled down version of C Library printf.
 * Only %s %u %d (==%u) %o %x are recognized.
 * Used to print diagnostic information
 * directly on console tty.
 * Since it is not interrupt driven,
 * all system activities are pretty much
 * suspended.
 * Printf should not be used for chit-chat.
 */
void printf(char *fmt, ...)
{
    register int c;
    va_list adx;
    char *s;
    int d;

    va_start(adx, fmt);
loop:
    while ((c = *fmt++) != '%') {
        if (c == '\0') {
            va_end(adx);
            return;
        }
        putchar(c);
    }
    c = *fmt++;
    if (c == 'd') {
        d = va_arg(adx, int);
        if (d < 0) {
            putchar('-');
            d = -d;
        }
        printn((long)d, 10);
    } else if (c == 'u' || c == 'o' || c == 'x')
        printn((long)va_arg(adx, unsigned), c == 'o' ? 8 : (c == 'x' ? 16 : 10));
    else if (c == 's') {
        s = va_arg(adx, char *);
        while ((c = *s++))
            putchar(c);
    }
    goto loop;
}

/*
 * Print an unsigned integer in base b.
 * Non-recursive to avoid deep kernel stacks.
 */
void printn(unsigned long n, int b)
{
    int prbuf[16]; /* 48-bit unsigned -> up to 16 octal digits */
    int *cp = prbuf;
    
    do {
        *cp++ = "0123456789abcdef"[n % b];
        n /= b;
    } while (n);
    do {
        putchar(*--cp);
    } while (cp > prbuf);
}

/*
 * Panic is called on unresolvable
 * fatal errors.
 * It syncs, prints "panic: mesg" and
 * then loops.
 */
void panic(char *s)
{
    panicstr = s;
    update();
    printf("panic: %s\n", s);
    for (;;)
        idle();
}

/*
 * prdev prints a warning message of the
 * form "mesg on dev x/y".
 * x and y are the major and minor parts of
 * the device argument.
 */
void prdev(char *str, dev_t dev)
{
    printf("%s on dev %u/%u\n", str, major(dev), minor(dev));
}

/*
 * deverr prints a diagnostic from
 * a device driver.
 * It prints the device, block number,
 * and an octal word (usually some error
 * status register) passed as argument.
 */
void deverror(register struct buf *bp, int o1, int o2)
{
    prdev("err", bp->b_dev);
    printf("bn=%d er=%o,%o\n", bp->b_blkno, o1, o2);
}
