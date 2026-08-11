/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// dc(1) -- task C15.  See dc.h for the number representation and README.md for the port.

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dc.h"

static jmp_buf mainloop;

int main(int argc, char **argv)
{
    init(argc, argv);

    // v7's onintr() called commnds() and never returned, leaving a commnds() frame behind
    // per interrupt.  The unwind runs here instead, at constant depth.
    if (setjmp(mainloop) != 0) {
        while (readptr != &readstk[0]) {
            if (*readptr != 0)
                release(*readptr);
            readptr--;
        }
        curfile = stdin;
    }
    commnds();
    return (0);
}

static void commnds(void)
{
    int c;
    struct blk *p, *q;
    long l;
    int sign;
    struct blk **ptr, *s, *t;
    struct sym *sp;
    int sk, sk1, sk2;
    int n, d;

    while (1) {
        if (((c = readc()) >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == '.') {
            unreadc(c);
            p = readin();
            pushp(p);
            continue;
        }
        switch (c) {
        case ' ':
        case '\n':
        case 0377:
        case EOF:
            continue;
        case 'Y':
            if (stkptr != &stack[0])
                sdump("stk", *stkptr);
            printf("all %d rel %d headmor %d\n", all, rel, headmor);
            printf("nbytes %d\n", nbytes);
            continue;
        case '_':
            p = readin();
            savk = sunputc(p);
            chsign(p);
            sputc(p, savk);
            pushp(p);
            continue;
        case '-':
            subt();
            continue;
        case '+':
            if (eqk() != 0)
                continue;
            binop('+');
            continue;
        case '*':
            arg1 = pop();
            EMPTY;
            arg2 = pop();
            EMPTYR(arg1);
            sk1 = sunputc(arg1);
            sk2 = sunputc(arg2);
            binop('*');
            p   = pop();
            sunputc(p);
            savk = sk1 + sk2;
            if (savk > k && savk > sk1 && savk > sk2) {
                sk = sk1;
                if (sk < sk2)
                    sk = sk2;
                if (sk < k)
                    sk = k;
                p    = removc(p, savk - sk);
                savk = sk;
            }
            sputc(p, savk);
            pushp(p);
            continue;
        case '/':
        casediv:
            if (dscale() != 0)
                continue;
            binop('/');
            if (irem != 0)
                release(irem);
            release(rem);
            continue;
        case '%':
            if (dscale() != 0)
                continue;
            binop('/');
            p = pop();
            release(p);
            if (irem == 0) {
                sputc(rem, skr + k);
                pushp(rem);
                continue;
            }
            p = add0(rem, skd - (skr + k));
            q = add(p, irem);
            release(p);
            release(irem);
            sputc(q, skd);
            pushp(q);
            continue;
        case 'v':
            p = pop();
            EMPTY;
            savk = sunputc(p);
            if (slen(p) == 0) {
                sputc(p, savk);
                pushp(p);
                continue;
            }
            if ((c = sbackc(p)) < 0) {
                error("sqrt of neg number\n");
            }
            if (k < savk)
                n = savk;
            else {
                n    = k * 2 - savk;
                savk = k;
            }
            arg1 = add0(p, n);
            arg2 = sqroot(arg1);
            sputc(arg2, savk);
            pushp(arg2);
            continue;
        case '^':
            expneg = 0;
            arg1   = pop();
            EMPTY;
            if (sunputc(arg1) != 0)
                error("exp not an integer\n");
            arg2 = pop();
            EMPTYR(arg1);
            if (sfbeg(arg1) == 0 && sbackc(arg1) < 0) {
                expneg++;
                chsign(arg1);
            }
            if (slen(arg1) >= 3) {
                error("exp too big\n");
            }
            savk = sunputc(arg2);
            p    = expn(arg2, arg1);
            release(arg2);
            srewind(arg1);
            c = sgetc(arg1);
            if (sfeof(arg1) == 0)
                c = sgetc(arg1) * 100 + c;
            d = c * savk;
            release(arg1);
            if (expneg == 0) {
                if (k >= savk)
                    n = k;
                else
                    n = savk;
                if (n < d) {
                    q = removc(p, d - n);
                    sputc(q, n);
                    pushp(q);
                } else {
                    sputc(p, d);
                    pushp(p);
                }
            } else {
                sputc(p, d);
                pushp(p);
            }
            if (expneg == 0)
                continue;
            p = pop();
            q = salloc(2);
            sputc(q, 1);
            sputc(q, 0);
            pushp(q);
            pushp(p);
            goto casediv;
        case 'z':
            p = salloc(2);
            n = stkptr - stkbeg;
            if (n >= 100) {
                sputc(p, n / 100);
                n %= 100;
            }
            sputc(p, n);
            sputc(p, 0);
            pushp(p);
            continue;
        case 'Z':
            p = pop();
            EMPTY;
            n = (slen(p) - 1) << 1;
            fsfile(p);
            sbackc(p);
            if (sfbeg(p) == 0) {
                if ((c = sbackc(p)) < 0) {
                    n -= 2;
                    if (sfbeg(p) == 1)
                        n += 1;
                    else {
                        if ((c = sbackc(p)) == 0)
                            n += 1;
                        else if (c > 90)
                            n -= 1;
                    }
                } else if (c < 10)
                    n -= 1;
            }
            release(p);
            q = salloc(1);
            if (n >= 100) {
                sputc(q, n % 100);
                n /= 100;
            }
            sputc(q, n);
            sputc(q, 0);
            pushp(q);
            continue;
        case 'i':
            p = pop();
            EMPTY;
            p = scalint(p);
            release(inbas);
            inbas = p;
            continue;
        case 'I':
            p = copy(inbas, slen(inbas) + 1);
            sputc(p, 0);
            pushp(p);
            continue;
        case 'o':
            p = pop();
            EMPTY;
            p    = scalint(p);
            sign = 0;
            n    = slen(p);
            q    = copy(p, n);
            fsfile(q);
            l = c = sbackc(q);
            if (n != 1) {
                if (c < 0) {
                    sign = 1;
                    chsign(q);
                    n = slen(q);
                    fsfile(q);
                    l = c = sbackc(q);
                }
                if (n != 1) {
                    while (sfbeg(q) == 0)
                        l = l * 100 + sbackc(q);
                }
            }
            logo  = log2v(l);
            obase = l;
            release(basptr);
            if (sign == 1)
                obase = -l;
            basptr = p;
            outdit = bigot;
            if (n == 1 && sign == 0) {
                if (c <= 16) {
                    outdit = hexot;
                    fw     = 1;
                    fw1    = 0;
                    ll     = 70;
                    release(q);
                    continue;
                }
            }
            n = 0;
            if (sign == 1)
                n++;
            p = salloc(1);
            sputc(p, -1);
            t = add(p, q);
            n += slen(t) * 2;
            fsfile(t);
            if ((c = sbackc(t)) > 9)
                n++;
            release(t);
            release(q);
            release(p);
            fw  = n;
            fw1 = n - 1;
            ll  = 70;
            if (fw >= ll)
                continue;
            ll = (70 / fw) * fw;
            continue;
        case 'O':
            p = copy(basptr, slen(basptr) + 1);
            sputc(p, 0);
            pushp(p);
            continue;
        case '[':
            n = 0;
            p = salloc(0);
            while (1) {
                if ((c = readc()) == ']') {
                    if (n == 0)
                        break;
                    n--;
                }
                sputc(p, c);
                if (c == '[')
                    n++;
            }
            pushp(p);
            continue;
        case 'k':
            p = pop();
            EMPTY;
            p = scalint(p);
            if (slen(p) > 1) {
                error("scale too big\n");
            }
            srewind(p);
            k = sfeof(p) ? 0 : sgetc(p);
            release(scalptr);
            scalptr = p;
            continue;
        case 'K':
            p = copy(scalptr, slen(scalptr) + 1);
            sputc(p, 0);
            pushp(p);
            continue;
        case 'X':
            p = pop();
            EMPTY;
            fsfile(p);
            n = sbackc(p);
            release(p);
            p = salloc(2);
            sputc(p, n);
            sputc(p, 0);
            pushp(p);
            continue;
        case 'Q':
            p = pop();
            EMPTY;
            if (slen(p) > 2) {
                error("Q?\n");
            }
            srewind(p);
            if ((c = sgetc(p)) < 0) {
                error("neg Q\n");
            }
            release(p);
            while (c-- > 0) {
                if (readptr == &readstk[0]) {
                    error("readstk?\n");
                }
                if (*readptr != 0)
                    release(*readptr);
                readptr--;
            }
            continue;
        case 'q':
            if (readptr <= &readstk[1])
                exit(0);
            if (*readptr != 0)
                release(*readptr);
            readptr--;
            if (*readptr != 0)
                release(*readptr);
            readptr--;
            continue;
        case 'f':
            if (stkptr == &stack[0])
                printf("empty stack\n");
            else {
                for (ptr = stkptr; ptr > &stack[0];) {
                    prtblk(*ptr--);
                }
            }
            continue;
        case 'p':
            if (stkptr == &stack[0])
                printf("empty stack\n");
            else {
                prtblk(*stkptr);
            }
            continue;
        case 'P':
            p = pop();
            EMPTY;
            sputc(p, 0);
            printf("%s", p->beg);
            release(p);
            continue;
        case 'd':
            if (stkptr == &stack[0]) {
                printf("empty stack\n");
                continue;
            }
            q = *stkptr;
            n = slen(q);
            p = copy(*stkptr, n);
            pushp(p);
            continue;
        case 'c':
            while (stkerr == 0) {
                p = pop();
                if (stkerr == 0)
                    release(p);
            }
            continue;
        case 'S':
            if (stkptr == &stack[0]) {
                error("save: args\n");
            }
            c        = readc() & 0377;
            sptr     = stable[c];
            sp = stable[c] = sfree;
            sfree          = sfree->next;
            if (sfree == 0)
                goto sempty;
            sp->next = sptr;
            p        = pop();
            EMPTY;
            if (c >= ARRAYST) {
                q = copy(p, PTRSZ);
                for (n = 0; n < PTRSZ - 1; n++)
                    sputc(q, 0);
                release(p);
                p = q;
            }
            sp->val = p;
            continue;
        sempty:
            error("symbol table overflow\n");
        case 's':
            if (stkptr == &stack[0]) {
                error("save:args\n");
            }
            c    = readc() & 0377;
            sptr = stable[c];
            if (sptr != 0) {
                p = sptr->val;
                if (c >= ARRAYST) {
                    srewind(p);
                    while (morewd(p)) {
                        q = getblkwd(p);
                        if (q != 0) // v7 released the null element too
                            release(q);
                    }
                }
                release(p);
            } else {
                sptr = stable[c] = sfree;
                sfree            = sfree->next;
                if (sfree == 0)
                    goto sempty;
                sptr->next = 0;
            }
            p         = pop();
            sptr->val = p;
            continue;
        case 'l':
            load();
            continue;
        case 'L':
            c    = readc() & 0377;
            sptr = stable[c];
            if (sptr == 0) {
                error("L?\n");
            }
            stable[c]  = sptr->next;
            sptr->next = sfree;
            sfree      = sptr;
            p          = sptr->val;
            if (c >= ARRAYST) {
                srewind(p);
                while (morewd(p)) {
                    q = getblkwd(p);
                    if (q != 0)
                        release(q);
                }
            }
            pushp(p);
            continue;
        case ':':
            p = pop();
            EMPTY;
            q = scalint(p);
            fsfile(q);
            c = 0;
            if ((sfbeg(q) == 0) && ((c = sbackc(q)) < 0)) {
                error("neg index\n");
            }
            if (slen(q) > 2) {
                error("index too big\n");
            }
            if (sfbeg(q) == 0)
                c = c * 100 + sbackc(q);
            if (c >= MAXIND) {
                error("index too big\n");
            }
            release(q);
            n    = readc() & 0377;
            sptr = stable[n];
            if (sptr == 0) {
                sptr = stable[n] = sfree;
                sfree            = sfree->next;
                if (sfree == 0)
                    goto sempty;
                sptr->next = 0;
                p          = salloc((c + PTRSZ) * PTRSZ);
                zeroblk(p);
            } else {
                p = sptr->val;
                if (slen(p) - PTRSZ < c * PTRSZ) {
                    q = copy(p, (c + PTRSZ) * PTRSZ);
                    release(p);
                    p = q;
                }
            }
            seekc(p, c * PTRSZ);
            q = lookwd(p);
            if (q != NULL)
                release(q);
            s = pop();
            EMPTY;
            salterwd(p, s);
            sptr->val = p;
            continue;
        case ';':
            p = pop();
            EMPTY;
            q = scalint(p);
            fsfile(q);
            c = 0;
            if ((sfbeg(q) == 0) && ((c = sbackc(q)) < 0)) {
                error("neg index\n");
            }
            if (slen(q) > 2) {
                error("index too big\n");
            }
            if (sfbeg(q) == 0)
                c = c * 100 + sbackc(q);
            if (c >= MAXIND) {
                error("index too big\n");
            }
            release(q);
            n    = readc() & 0377;
            sptr = stable[n];
            if (sptr != 0) {
                p = sptr->val;
                if (slen(p) - PTRSZ >= c * PTRSZ) {
                    seekc(p, c * PTRSZ);
                    s = getblkwd(p);
                    if (s != 0) {
                        q = copy(s, slen(s));
                        pushp(q);
                        continue;
                    }
                }
            }
            // An unset element reads as 0, as an unset register does in load().  v7 pushed
            // a PTRSZ-sized block of nulls, which prints as one 0 only because a PDP-11
            // pointer is two bytes; six of them here printed nine.
            q = salloc(1);
            sputc(q, 0);
            pushp(q);
            continue;
        case 'x':
        execute:
            p = pop();
            EMPTY;
            // v7 incremented readptr before testing it, so the diagnostic fired one slot
            // late and *readptr wrote past the array.
            if ((readptr != &readstk[0]) && (*readptr != 0) &&
                ((*readptr)->rd == (*readptr)->wt))
                release(*readptr);
            else {
                if (readptr == &readstk[RDSKSZ - 1]) {
                    error("nesting depth\n");
                }
                readptr++;
            }
            *readptr = p;
            if (p != 0)
                srewind(p);
            else {
                if ((c = readc()) != '\n')
                    unreadc(c);
            }
            continue;
        case '?':
            if (readptr == &readstk[RDSKSZ - 1]) {
                error("nesting depth\n");
            }
            readptr++;
            *readptr = 0;
            fsave    = curfile;
            curfile  = stdin;
            while ((c = readc()) == '!')
                command();
            p = salloc(0);
            sputc(p, c);
            while ((c = readc()) != '\n') {
                sputc(p, c);
                if (c == '\\')
                    sputc(p, readc());
            }
            curfile  = fsave;
            *readptr = p;
            continue;
        case '!':
            if (command() == 1)
                goto execute;
            continue;
        case '<':
        case '>':
        case '=':
            if (cond(c) == 1)
                goto execute;
            continue;
        default:
            // Masked, so that a byte above 0177 reads the same whether it was typed (getc
            // masks) or came out of a macro block (sgetc sign-extends).
            printf("%o is unimplemented\n", c & 0377);
        }
    }
}

static struct blk *divide(struct blk *ddivd, struct blk *ddivr)
{
    int divsign, remsign, offset, divcarry;
    int carry, dig, magic, d, dd;
    long c, td, cc;
    struct blk *ps;
    struct blk *p, *divd, *divr;

    rem = 0;
    p   = salloc(0);
    if (slen(ddivr) == 0) {
        // v7 returned the int 1 from here and pushed its argument.  Both were right only
        // for the caller dscale() now guards; scale() reaches this with a zero base.
        printf("divide by 0\n");
        rem = salloc(0);
        return (p);
    }
    divsign = remsign = 0;
    divr              = ddivr;
    fsfile(divr);
    if (sbackc(divr) == -1) {
        divr = copy(ddivr, slen(ddivr));
        chsign(divr);
        divsign = ~divsign;
    }
    divd = copy(ddivd, slen(ddivd));
    fsfile(divd);
    if (sfbeg(divd) == 0 && sbackc(divd) == -1) {
        chsign(divd);
        divsign = ~divsign;
        remsign = ~remsign;
    }
    offset = slen(divd) - slen(divr);
    if (offset < 0)
        goto ddone;
    seekc(p, offset + 1);
    sputc(divd, 0);
    magic = 0;
    fsfile(divr);
    c = sbackc(divr);
    if (c < 10)
        magic++;
    c = c * 100 + (sfbeg(divr) ? 0 : sbackc(divr));
    if (magic > 0) {
        c = (c * 100 + (sfbeg(divr) ? 0 : sbackc(divr))) * 2;
        c /= 25;
    }
    while (offset >= 0) {
        fsfile(divd);
        td = sbackc(divd) * 100;
        dd = sfbeg(divd) ? 0 : sbackc(divd);
        td = (td + dd) * 100;
        dd = sfbeg(divd) ? 0 : sbackc(divd);
        td = td + dd;
        cc = c;
        if (offset == 0)
            td += 1;
        else
            cc += 1;
        if (magic != 0)
            td = td << 3;
        dig = td / cc;
        srewind(divr);
        srewind(divxyz);
        carry = 0;
        while (sfeof(divr) == 0) {
            d     = sgetc(divr) * dig + carry;
            carry = d / 100;
            salterc(divxyz, d % 100);
        }
        salterc(divxyz, carry);
        srewind(divxyz);
        seekc(divd, offset);
        carry = 0;
        while (sfeof(divd) == 0) {
            d     = slookc(divd);
            d     = d - (sfeof(divxyz) ? 0 : sgetc(divxyz)) - carry;
            carry = 0;
            if (d < 0) {
                d += 100;
                carry = 1;
            }
            salterc(divd, d);
        }
        divcarry = carry;
        sbackc(p);
        salterc(p, dig);
        sbackc(p);
        if (--offset >= 0)
            divd->wt--;
    }
    if (divcarry != 0) {
        salterc(p, dig - 1);
        salterc(divd, -1);
        ps = add(divr, divd);
        release(divd);
        divd = ps;
    }

    srewind(p);
    divcarry = 0;
    while (sfeof(p) == 0) {
        d        = slookc(p) + divcarry;
        divcarry = 0;
        if (d >= 100) {
            d -= 100;
            divcarry = 1;
        }
        salterc(p, d);
    }
    if (divcarry != 0)
        salterc(p, divcarry);
    fsfile(p);
    while (sfbeg(p) == 0) {
        if (sbackc(p) == 0)
            strunc(p);
        else
            break;
    }
    if (divsign < 0)
        chsign(p);
    fsfile(divd);
    while (sfbeg(divd) == 0) {
        if (sbackc(divd) == 0)
            strunc(divd);
        else
            break;
    }
ddone:
    if (remsign < 0)
        chsign(divd);
    if (divr != ddivr)
        release(divr);
    rem = divd;
    return (p);
}

static int dscale(void)
{
    struct blk *dd, *dr;
    struct blk *r;
    int c;

    dr = pop();
    EMPTYS;
    dd = pop();
    EMPTYSR(dr);
    fsfile(dd);
    skd = sunputc(dd);
    fsfile(dr);
    skr = sunputc(dr);
    if (sfbeg(dr) == 1 || (sfbeg(dr) == 0 && sbackc(dr) == 0)) {
        sputc(dr, skr);
        pushp(dr);
        errorrt("divide by 0\n");
    }
    c = k - skd + skr;
    if (c < 0)
        r = removr(dd, -c);
    else {
        r    = add0(dd, c);
        irem = 0;
    }
    arg1 = r;
    arg2 = dr;
    savk = k;
    return (0);
}

static struct blk *removr(struct blk *p, int n)
{
    int nn;
    struct blk *q, *s, *r;

    srewind(p);
    nn = (n + 1) / 2;
    q  = salloc(nn);
    while (n > 1) {
        sputc(q, sgetc(p));
        n -= 2;
    }
    r = salloc(2);
    while (sfeof(p) == 0)
        sputc(r, sgetc(p));
    release(p);
    if (n == 1) {
        s = divide(r, tenptr);
        release(r);
        srewind(rem);
        if (sfeof(rem) == 0)
            sputc(q, sgetc(rem));
        release(rem);
        irem = q;
        return (s);
    }
    irem = q;
    return (r);
}

static struct blk *sqroot(struct blk *p)
{
    struct blk *t;
    struct blk *r, *q, *s;
    int c, n, nn;

    n = slen(p);
    fsfile(p);
    c = sbackc(p);
    if ((n & 1) != 1)
        c = c * 100 + (sfbeg(p) ? 0 : sbackc(p));
    n = (n + 1) >> 1;
    r = salloc(n);
    zeroblk(r);
    seekc(r, n);
    nn = 1;
    while ((c -= nn) >= 0)
        nn += 2;
    c = (nn + 1) >> 1;
    fsfile(r);
    sbackc(r);
    if (c >= 100) {
        c -= 100;
        salterc(r, c);
        sputc(r, 1);
    } else
        salterc(r, c);
    while (1) {
        q = divide(p, r);
        s = add(q, r);
        release(q);
        release(rem);
        q = divide(s, sqtemp);
        release(s);
        release(rem);
        s = copy(r, slen(r));
        chsign(s);
        t = add(s, q);
        release(s);
        fsfile(t);
        nn = sfbeg(t) ? 0 : sbackc(t);
        if (nn >= 0)
            break;
        release(r);
        release(t);
        r = q;
    }
    release(t);
    release(q);
    release(p);
    return (r);
}

static struct blk *expn(struct blk *base, struct blk *ex)
{
    struct blk *r, *e, *p;
    struct blk *e1, *t, *cp;
    int temp, c, n;

    r = salloc(1);
    sputc(r, 1);
    p = copy(base, slen(base));
    e = copy(ex, slen(ex));
    fsfile(e);
    if (sfbeg(e) != 0)
        goto edone;
    temp = 0;
    c    = sbackc(e);
    if (c < 0) {
        temp++;
        chsign(e);
    }
    while (slen(e) != 0) {
        e1 = divide(e, sqtemp);
        release(e);
        e = e1;
        n = slen(rem);
        release(rem);
        if (n != 0) {
            e1 = mult(p, r);
            release(r);
            r = e1;
        }
        t  = copy(p, slen(p));
        cp = mult(p, t);
        release(p);
        release(t);
        p = cp;
    }
    if (temp != 0) {
        if ((c = slen(base)) == 0) {
            goto edone;
        }
        if (c > 1)
            screate(r);
        else {
            srewind(base);
            if ((c = sgetc(base)) <= 1) {
                screate(r);
                sputc(r, c);
            } else
                screate(r);
        }
    }
edone:
    release(p);
    release(e);
    return (r);
}

static void init(int argc, char **argv)
{
    struct sym *sp;

    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, onintr);
    // v7 asked for unbuffered here -- one write(2) per printed character.  Nothing is asked
    // for instead: _flsbuf() line-buffers stdout when isatty(1) and buffers it fully when it
    // does not, which is what each case wants.  readc() and command() flush before they
    // block, so a prompt and a `P' still reach the terminal on time.
    svargc = --argc;
    svargv = argv;
    while (svargc > 0 && svargv[1][0] == '-') {
        svargc--;
        svargv++;
    }
    if (svargc <= 0)
        curfile = stdin;
    else if ((curfile = fopen(svargv[1], "r")) == NULL) {
        printf("can't open file %s\n", svargv[1]);
        exit(1);
    }
    dummy   = malloc(1);
    scalptr = salloc(1);
    sputc(scalptr, 0);
    basptr = salloc(1);
    sputc(basptr, 10);
    obase  = 10;
    logten = log2v(10L);
    ll     = 70;
    fw     = 1;
    fw1    = 0;
    tenptr = salloc(1);
    sputc(tenptr, 10);
    obase = 10;
    inbas = salloc(1);
    sputc(inbas, 10);
    sqtemp = salloc(1);
    sputc(sqtemp, 2);
    chptr  = salloc(0);
    strptr = salloc(0);
    divxyz = salloc(0);
    stkbeg = stkptr = &stack[0];
    // v7 said &stack[STKSZ], so the guard fired only after *++stkptr had written past it.
    stkend  = &stack[STKSZ - 1];
    stkerr  = 0;
    readptr = &readstk[0];
    k       = 0;
    sp = sptr = &symlst[0];
    while (sptr < &symlst[TBLSZ]) {
        sptr->next = ++sp;
        sptr++;
    }
    sptr->next = 0;
    sfree      = &symlst[0];
}

static void onintr(int sig)
{
    (void)sig;
    signal(SIGINT, onintr); // v7: delivery resets a caught signal to SIG_DFL
    longjmp(mainloop, 1);
}

static void pushp(struct blk *p)
{
    if (stkptr == stkend) {
        printf("out of stack space\n");
        return;
    }
    stkerr   = 0;
    *++stkptr = p;
}

static struct blk *pop(void)
{
    if (stkptr == stack) {
        stkerr = 1;
        return (0);
    }
    return (*stkptr--);
}

static struct blk *readin(void)
{
    struct blk *p, *q;
    int dp, dpct;
    int c;

    dp = dpct = 0;
    p         = salloc(0);
    while (1) {
        c = readc();
        switch (c) {
        case '.':
            if (dp != 0) {
                unreadc(c);
                break;
            }
            dp++;
            continue;
        case '\\':
            readc();
            continue;
        default:
            if (c >= 'A' && c <= 'F')
                c = c - 'A' + 10;
            else if (c >= '0' && c <= '9')
                c -= '0';
            else
                goto gotnum;
            if (dp != 0) {
                if (dpct >= 99)
                    continue;
                dpct++;
            }
            screate(chptr);
            if (c != 0)
                sputc(chptr, c);
            q = mult(p, inbas);
            release(p);
            p = add(chptr, q);
            release(q);
        }
    }
gotnum:
    unreadc(c);
    if (dp == 0) {
        sputc(p, 0);
        return (p);
    } else {
        q = scale(p, dpct);
        return (q);
    }
}

// returns pointer to struct with ct 0's & p
static struct blk *add0(struct blk *p, int ct)
{
    struct blk *q, *t;

    q = salloc(slen(p) + (ct + 1) / 2);
    while (ct > 1) {
        sputc(q, 0);
        ct -= 2;
    }
    srewind(p);
    while (sfeof(p) == 0) {
        sputc(q, sgetc(p));
    }
    release(p);
    if (ct == 1) {
        t = mult(tenptr, q);
        release(q);
        return (t);
    }
    return (q);
}

static struct blk *mult(struct blk *p, struct blk *q)
{
    struct blk *mp, *mq, *mr;
    int sign, offset, carry;
    int cq, cp, mt, mcr;

    offset = sign = 0;
    fsfile(p);
    mp = p;
    if (sfbeg(p) == 0) {
        if (sbackc(p) < 0) {
            mp = copy(p, slen(p));
            chsign(mp);
            sign = ~sign;
        }
    }
    fsfile(q);
    mq = q;
    if (sfbeg(q) == 0) {
        if (sbackc(q) < 0) {
            mq = copy(q, slen(q));
            chsign(mq);
            sign = ~sign;
        }
    }
    mr = salloc(slen(mp) + slen(mq));
    zeroblk(mr);
    srewind(mq);
    while (sfeof(mq) == 0) {
        cq = sgetc(mq);
        srewind(mp);
        srewind(mr);
        mr->rd += offset;
        carry = 0;
        while (sfeof(mp) == 0) {
            cp    = sgetc(mp);
            mcr   = sfeof(mr) ? 0 : slookc(mr);
            mt    = cp * cq + carry + mcr;
            carry = mt / 100;
            salterc(mr, mt % 100);
        }
        offset++;
        if (carry != 0) {
            mcr = sfeof(mr) ? 0 : slookc(mr);
            salterc(mr, mcr + carry);
        }
    }
    if (sign < 0) {
        chsign(mr);
    }
    if (mp != p)
        release(mp);
    if (mq != q)
        release(mq);
    return (mr);
}

static void chsign(struct blk *p)
{
    int carry;
    int ct; // a cell, and 100-slookc()-carry: not a char

    carry = 0;
    srewind(p);
    while (sfeof(p) == 0) {
        ct    = 100 - slookc(p) - carry;
        carry = 1;
        if (ct >= 100) {
            ct -= 100;
            carry = 0;
        }
        salterc(p, ct);
    }
    if (carry != 0) {
        sputc(p, -1);
        fsfile(p);
        sbackc(p);
        ct = sbackc(p);
        if (ct == 99) {
            strunc(p);
            sputc(p, -1);
        }
    } else {
        fsfile(p);
        ct = sbackc(p);
        if (ct == 0)
            strunc(p);
    }
}

static int readc(void)
{
    int c;

loop:
    if ((readptr != &readstk[0]) && (*readptr != 0)) {
        if (sfeof(*readptr) == 0)
            return (sgetc(*readptr));
        release(*readptr);
        readptr--;
        goto loop;
    }
    if (curfile == stdin)
        fflush(stdout); // stdout is line buffered; show the prompt before blocking
    c = getc(curfile);
    if (c != EOF)
        return (c);
    // v7 wrote &readptr[0], which is readptr, so this branch never ran.
    if (readptr != &readstk[0]) {
        readptr--;
        if (*readptr == 0)
            curfile = stdin;
        goto loop;
    }
    if (curfile != stdin) {
        fclose(curfile);
        curfile = stdin;
        goto loop;
    }
    exit(0);
}

static void unreadc(int c)
{
    if (c == EOF) // readin() passes it, and a char parameter turned it into a real 0377
        return;
    if ((readptr != &readstk[0]) && (*readptr != 0)) {
        sungetc(*readptr, c);
    } else
        ungetc(c, curfile);
}

static void binop(int c)
{
    struct blk *r;

    r = 0;
    switch (c) {
    case '+':
        r = add(arg1, arg2);
        break;
    case '*':
        r = mult(arg1, arg2);
        break;
    case '/':
        r = divide(arg1, arg2);
        break;
    }
    release(arg1);
    release(arg2);
    sputc(r, savk);
    pushp(r);
}

// A block carries no type, so print guesses.  v7 called it a string if any cell exceeded 99,
// which on a signed-char machine misreads an eight-bit byte; read raw, the 0377 sign cell
// misreads too.  Read raw and allow 0377 only where a sign cell can sit: the top magnitude
// byte, the scale byte being last.  README.md.
static int isstring(struct blk *p)
{
    int n, i, c;
    char *q;

    n = slen(p);
    q = p->beg;
    for (i = 0; i < n; i++) {
        c = *q++ & 0377;
        if (c > 99 && !(c == 0377 && i == n - 2))
            return (1);
    }
    return (0);
}

static void prtblk(struct blk *hptr)
{
    int sc;
    struct blk *p, *q, *dec;
    int dig, dout, ct;

    if (isstring(hptr)) {
        srewind(hptr);
        while (sfeof(hptr) == 0)
            printf("%c", *hptr->rd++ & 0377);
        printf("\n");
        return;
    }
    fsfile(hptr);
    sc = sbackc(hptr);
    if (sfbeg(hptr) != 0) {
        printf("0\n");
        return;
    }
    count = ll;
    p     = copy(hptr, slen(hptr));
    sunputc(p);
    fsfile(p);
    if (sbackc(p) < 0) {
        chsign(p);
        OUTC('-');
    }
    if ((obase == 0) || (obase == -1)) {
        oneot(p, sc, 'd');
        return;
    }
    if (obase == 1) {
        oneot(p, sc, '1');
        return;
    }
    if (obase == 10) {
        tenot(p, sc);
        return;
    }
    screate(strptr);
    dig  = logten * sc;
    dout = ((dig / 10) + dig) / logo;
    dec  = getdec(p, sc);
    p    = removc(p, sc);
    while (slen(p) != 0) {
        q = divide(p, basptr);
        release(p);
        p = q;
        (*outdit)(rem, 0);
    }
    release(p);
    fsfile(strptr);
    while (sfbeg(strptr) == 0)
        OUTC(sbackc(strptr));
    if (sc == 0) {
        release(dec);
        printf("\n");
        return;
    }
    screate(strptr);
    OUTC('.');
    ct = 0;
    do {
        q = mult(basptr, dec);
        release(dec);
        dec = getdec(q, sc);
        p   = removc(q, sc);
        (*outdit)(p, 1);
    } while (++ct < dout);
    release(dec);
    srewind(strptr);
    while (sfeof(strptr) == 0)
        OUTC(sgetc(strptr));
    printf("\n");
}

static struct blk *getdec(struct blk *p, int sc)
{
    int cc;
    struct blk *q, *t, *s;

    srewind(p);
    if (slen(p) * 2 < sc) {
        q = copy(p, slen(p));
        return (q);
    }
    q = salloc(slen(p));
    while (sc >= 1) {
        sputc(q, sgetc(p));
        sc -= 2;
    }
    if (sc != 0) {
        t = mult(q, tenptr);
        s = salloc(cc = slen(q));
        release(q);
        srewind(t);
        while (cc-- > 0)
            sputc(s, sgetc(t));
        sputc(s, 0);
        release(t);
        t = divide(s, tenptr);
        release(s);
        release(rem);
        return (t);
    }
    return (q);
}

static void tenot(struct blk *p, int sc)
{
    int c, f;

    fsfile(p);
    f = 0;
    while ((sfbeg(p) == 0) && ((p->rd - p->beg - 1) * 2 >= sc)) {
        c = sbackc(p);
        if ((c < 10) && (f == 1))
            printf("0%d", c);
        else
            printf("%d", c);
        f = 1;
        TEST2;
    }
    if (sc == 0) {
        printf("\n");
        release(p);
        return;
    }
    if ((p->rd - p->beg) * 2 > sc) {
        c = sbackc(p);
        printf("%d.", c / 10);
        TEST2;
        OUTC(c % 10 + '0');
        sc--;
    } else {
        OUTC('.');
    }
    if (sc > (p->rd - p->beg) * 2) {
        while (sc > (p->rd - p->beg) * 2) {
            OUTC('0');
            sc--;
        }
    }
    while (sc > 1) {
        c = sbackc(p);
        if (c < 10)
            printf("0%d", c);
        else
            printf("%d", c);
        sc -= 2;
        TEST2;
    }
    if (sc == 1) {
        OUTC(sbackc(p) / 10 + '0');
    }
    printf("\n");
    release(p);
}

static void oneot(struct blk *p, int sc, int ch)
{
    struct blk *q;

    q = removc(p, sc);
    screate(strptr);
    sputc(strptr, -1);
    while (slen(q) > 0) {
        p = add(strptr, q);
        release(q);
        q = p;
        OUTC(ch);
    }
    release(q);
    printf("\n");
}

static void hexot(struct blk *p, int flg)
{
    int c;

    (void)flg;
    srewind(p);
    if (sfeof(p) != 0) {
        sputc(strptr, '0');
        release(p);
        return;
    }
    c = sgetc(p);
    release(p);
    if (c >= 16) {
        printf("hex digit > 16\n");
        return;
    }
    sputc(strptr, c < 10 ? c + '0' : c - 10 + 'A');
}

static void bigot(struct blk *p, int flg)
{
    struct blk *t, *q;
    int l;
    int neg;

    l = 0;
    if (flg == 1)
        t = salloc(0);
    else {
        t = strptr;
        l = slen(strptr) + fw - 1;
    }
    neg = 0;
    if (slen(p) != 0) {
        fsfile(p);
        if (sbackc(p) < 0) {
            neg = 1;
            chsign(p);
        }
        while (slen(p) != 0) {
            q = divide(p, tenptr);
            release(p);
            p = q;
            srewind(rem);
            sputc(t, sfeof(rem) ? '0' : sgetc(rem) + '0');
            release(rem);
        }
    }
    release(p);
    if (flg == 1) {
        l = fw1 - slen(t);
        if (neg != 0) {
            l--;
            sputc(strptr, '-');
        }
        fsfile(t);
        while (l-- > 0)
            sputc(strptr, '0');
        while (sfbeg(t) == 0)
            sputc(strptr, sbackc(t));
        release(t);
    } else {
        l -= slen(strptr);
        while (l-- > 0)
            sputc(strptr, '0');
        if (neg != 0) {
            sunputc(strptr);
            sputc(strptr, '-');
        }
    }
    sputc(strptr, ' ');
}

static struct blk *add(struct blk *a1, struct blk *a2)
{
    struct blk *p;
    int carry, n;
    int size;
    int c, n1, n2;

    size = slen(a1) > slen(a2) ? slen(a1) : slen(a2);
    p    = salloc(size);
    srewind(a1);
    srewind(a2);
    carry = 0;
    while (--size >= 0) {
        n1 = sfeof(a1) ? 0 : sgetc(a1);
        n2 = sfeof(a2) ? 0 : sgetc(a2);
        n  = n1 + n2 + carry;
        if (n >= 100) {
            carry = 1;
            n -= 100;
        } else if (n < 0) {
            carry = -1;
            n += 100;
        } else
            carry = 0;
        sputc(p, n);
    }
    if (carry != 0)
        sputc(p, carry);
    fsfile(p);
    if (sfbeg(p) == 0) {
        while (sfbeg(p) == 0 && (c = sbackc(p)) == 0)
            ;
        if (c != 0)
            salterc(p, c);
        strunc(p);
    }
    fsfile(p);
    if (sfbeg(p) == 0 && sbackc(p) == -1) {
        while ((c = sbackc(p)) == 99) {
            if (c == EOF)
                break;
        }
        sgetc(p);
        salterc(p, -1);
        strunc(p);
    }
    return (p);
}

static int eqk(void)
{
    struct blk *p, *q;
    int skp;
    int skq;

    p = pop();
    EMPTYS;
    q = pop();
    EMPTYSR(p);
    skp = sunputc(p);
    skq = sunputc(q);
    if (skp == skq) {
        arg1 = p;
        arg2 = q;
        savk = skp;
        return (0);
    } else if (skp < skq) {
        savk = skq;
        p    = add0(p, skq - skp);
    } else {
        savk = skp;
        q    = add0(q, skp - skq);
    }
    arg1 = p;
    arg2 = q;
    return (0);
}

static struct blk *removc(struct blk *p, int n)
{
    struct blk *q, *r;

    srewind(p);
    while (n > 1) {
        sgetc(p);
        n -= 2;
    }
    q = salloc(2);
    while (sfeof(p) == 0)
        sputc(q, sgetc(p));
    if (n == 1) {
        r = divide(q, tenptr);
        release(q);
        release(rem);
        q = r;
    }
    release(p);
    return (q);
}

static struct blk *scalint(struct blk *p)
{
    int n;

    n = sunputc(p);
    p = removc(p, n);
    return (p);
}

static struct blk *scale(struct blk *p, int n)
{
    struct blk *q, *s, *t;

    t = add0(p, n);
    q = salloc(1);
    sputc(q, n);
    s = expn(inbas, q);
    release(q);
    q = divide(t, s);
    release(t);
    release(s);
    release(rem);
    sputc(q, n);
    return (q);
}

static int subt(void)
{
    arg1 = pop();
    EMPTYS;
    savk = sunputc(arg1);
    chsign(arg1);
    sputc(arg1, savk);
    pushp(arg1);
    if (eqk() != 0)
        return (1);
    binop('+');
    return (0);
}

static int command(void)
{
    int c;
    char line[100];
    int n;
    void (*savint)(int);
    int pid, rpid;

    switch (c = readc()) {
    case '<':
        return (cond(NL));
    case '>':
        return (cond(NG));
    case '=':
        return (cond(NE));
    default:
        // v7 wrote the line into a 100-byte automatic with no bound at all.
        n         = 0;
        line[n++] = c;
        while ((c = readc()) != '\n')
            if (n < (int)sizeof line - 1)
                line[n++] = c;
        line[n] = 0;
        fflush(stdout);
        if ((pid = fork()) == 0) {
            execl("/bin/sh", "sh", "-c", line, (char *)NULL);
            _exit(0100);
        }
        savint = signal(SIGINT, SIG_IGN);
        while ((rpid = wait((int *)0)) != pid && rpid != -1)
            ;
        signal(SIGINT, savint);
        printf("!\n");
        return (0);
    }
}

static int cond(int c)
{
    struct blk *p;
    int cc;

    if (subt() != 0)
        return (1);
    p = pop();
    sunputc(p);
    if (slen(p) == 0) {
        release(p);
        if (c == '<' || c == '>' || c == NE) {
            readc();
            return (0);
        }
        load();
        return (1);
    } else {
        if (c == '=') {
            release(p);
            readc();
            return (0);
        }
    }
    if (c == NE) {
        release(p);
        load();
        return (1);
    }
    fsfile(p);
    cc = sbackc(p);
    release(p);
    if ((cc < 0 && (c == '<' || c == NG)) || (cc > 0 && (c == '>' || c == NL))) {
        readc();
        return (0);
    }
    load();
    return (1);
}

static void load(void)
{
    int c;
    struct blk *p, *q;
    struct blk *t, *s;

    c    = readc() & 0377;
    sptr = stable[c];
    if (sptr != 0) {
        p = sptr->val;
        if (c >= ARRAYST) {
            q = salloc(slen(p));
            srewind(p);
            while (morewd(p)) {
                s = getblkwd(p);
                if (s == 0) {
                    putwd(q, (struct blk *)NULL);
                } else {
                    t = copy(s, slen(s));
                    putwd(q, t);
                }
            }
            pushp(q);
        } else {
            q = copy(p, slen(p));
            pushp(q);
        }
    } else {
        q = salloc(1);
        sputc(q, 0);
        pushp(q);
    }
}

// floor(log2(n)) for n > 0.  v7 counted down from 31, the top bit of a PDP-11 long; an int
// here is 41 bits and the count came out wrong, skewing prtblk()'s fraction digit count.
static int log2v(long n)
{
    int i;

    if (n <= 0)
        return (0);
    for (i = 0; n > 1; n >>= 1)
        i++;
    return (i);
}

static struct blk *salloc(int size)
{
    struct blk *hdr;
    char *ptr;

    all++;
    nbytes += size;
    ptr = malloc(size);
    if (ptr == 0) {
        garbage();
        if ((ptr = malloc(size)) == 0)
            ospace("salloc");
    }
    if ((hdr = hfree) == 0)
        hdr = morehd();
    hfree     = (struct blk *)hdr->rd;
    hdr->rd = hdr->wt = hdr->beg = ptr;
    hdr->last                    = ptr + size;
    return (hdr);
}

static struct blk *morehd(void)
{
    struct blk *h, *kk;

    headmor++;
    nbytes += HEADSZ;
    hfree = h = (struct blk *)malloc(HEADSZ);
    if (hfree == 0) {
        garbage();
        if ((hfree = h = (struct blk *)malloc(HEADSZ)) == 0)
            ospace("headers");
    }
    kk = h;
    while (h < hfree + (HEADSZ / BLK))
        (h++)->rd = (char *)++kk;
    (--h)->rd = 0;
    return (hfree);
}

static struct blk *copy(struct blk *hptr, int size)
{
    struct blk *hdr;
    int sz;
    char *ptr;

    all++;
    nbytes += size;
    sz = slen(hptr);
    // v7 set wt from the SOURCE's length.  A source longer than size put wt past last, and
    // sputc's `wt == last' guard then never fired -- reachable through an eight-bit S name.
    if (sz > size)
        sz = size;
    ptr = nalloc(hptr->beg, size, sz);
    if (ptr == 0) {
        garbage();
        if ((ptr = nalloc(hptr->beg, size, sz)) == NULL) {
            printf("copy size %d\n", size);
            ospace("copy");
        }
    }
    if ((hdr = hfree) == 0)
        hdr = morehd();
    hfree            = (struct blk *)hdr->rd;
    hdr->rd = hdr->beg = ptr;
    hdr->last          = ptr + size;
    hdr->wt            = ptr + sz;
    memset(hdr->wt, 0, hdr->last - hdr->wt);
    return (hdr);
}

static void sdump(char *s1, struct blk *hptr)
{
    char *p;

    printf("%s %o rd %o wt %o beg %o last %o\n", s1, (unsigned)hptr, (unsigned)hptr->rd,
           (unsigned)hptr->wt, (unsigned)hptr->beg, (unsigned)hptr->last);
    p = hptr->beg;
    while (p < hptr->wt)
        printf("%d ", *p++);
    printf("\n");
}

static void seekc(struct blk *hptr, int n)
{
    char *nn, *p;

    nn = hptr->beg + n;
    if (nn > hptr->last) {
        nbytes += nn - hptr->last;
        free(hptr->beg);
        p = realloc(hptr->beg, n);
        if (p == 0) {
            hptr->beg = realloc(hptr->beg, hptr->last - hptr->beg);
            garbage();
            if ((p = realloc(hptr->beg, n)) == 0)
                ospace("seekc");
        }
        hptr->beg = p;
        hptr->wt = hptr->last = hptr->rd = p + n;
        return;
    }
    hptr->rd = nn;
    if (nn > hptr->wt)
        hptr->wt = nn;
}

static void salterwd(struct blk *hptr, struct blk *n)
{
    if (hptr->rd == hptr->last)
        sgrow(hptr);
    wdat(hptr->rd) = n;
    hptr->rd += PTRSZ;
    if (hptr->rd > hptr->wt)
        hptr->wt = hptr->rd;
}

static void sgrow(struct blk *hptr)
{
    int size;
    char *p;

    if ((size = (hptr->last - hptr->beg) * 2) == 0)
        size = 1;
    nbytes += size / 2;
    free(hptr->beg);
    p = realloc(hptr->beg, size);
    if (p == 0) {
        hptr->beg = realloc(hptr->beg, hptr->last - hptr->beg);
        garbage();
        if ((p = realloc(hptr->beg, size)) == 0)
            ospace("sgrow");
    }
    hptr->rd   = hptr->rd - hptr->beg + p;
    hptr->wt   = hptr->wt - hptr->beg + p;
    hptr->beg  = p;
    hptr->last = p + size;
}

_Noreturn static void ospace(char *s)
{
    printf("out of space: %s\n", s);
    printf("all %d rel %d headmor %d\n", all, rel, headmor);
    printf("nbytes %d\n", nbytes);
    if (stkptr != &stack[0])
        sdump("stk", *stkptr);
    fflush(stdout); // abort() raises SIGABRT, and nothing else would flush
    abort();
}

// Compact the arena: free every block and realloc it back, which our malloc answers by
// coalescing forward (lib/libc/gen/malloc.c).  v7 also tested each block's low address bit
// for odd byte alignment, which on a word machine is a live address bit and would abort.
static void garbage(void)
{
    int i;
    struct blk *p, *q;
    struct sym *tmps;

    for (i = 0; i < TBLSZ; i++) {
        tmps = stable[i];
        if (tmps == 0)
            continue;
        if (i < ARRAYST) {
            do {
                redef(tmps->val);
                tmps = tmps->next;
            } while (tmps != 0);
        } else {
            do {
                p = tmps->val;
                srewind(p);
                // v7 stopped at the first null ELEMENT, having no way to tell one from the
                // end of the block.
                while (morewd(p)) {
                    q = getblkwd(p);
                    if (q != 0)
                        redef(q);
                }
                tmps = tmps->next;
            } while (tmps != 0);
        }
    }
}

static void redef(struct blk *p)
{
    int offset;
    char *newp;

    free(p->beg);
    free(dummy);
    dummy = malloc(1);
    if (dummy == NULL)
        ospace("dummy");
    newp = realloc(p->beg, p->last - p->beg);
    if (newp == NULL)
        ospace("redef");
    offset = newp - p->beg;
    p->beg = newp;
    p->rd += offset;
    p->wt += offset;
    p->last += offset;
}

static void release(struct blk *p)
{
    rel++;
    nbytes -= p->last - p->beg;
    p->rd = (char *)hfree;
    hfree = p;
    free(p->beg);
}

static struct blk *getblkwd(struct blk *p)
{
    struct blk *v;

    if (!morewd(p))
        return (NULL);
    v = wdat(p->rd);
    p->rd += PTRSZ;
    return (isblkptr(v) ? v : NULL);
}

static void putwd(struct blk *p, struct blk *c)
{
    if (p->wt == p->last)
        sgrow(p);
    wdat(p->wt) = c;
    p->wt += PTRSZ;
}

static struct blk *lookwd(struct blk *p)
{
    struct blk *v;

    if (!morewd(p))
        return (NULL);
    v = wdat(p->rd);
    return (isblkptr(v) ? v : NULL);
}

// n bytes of storage carrying the first `have' of p.  v7 copied n regardless, reading past
// the source whenever copy() grew a block -- 12 KB past it, for an array.
static char *nalloc(char *p, int n, int have)
{
    char *q;

    q = malloc(n);
    if (q == 0)
        return (0);
    memcpy(q, p, have);
    return (q);
}
