//
// Assembler for BESM-6.
// First pass: code generation into temporary segment files.
//
#include <stdio.h>

#include "as.h"

static void puthr(long h, long r)
{
    static long sh, sr;

    if (count[segm] & 01) {
        fputh(h, sfile[segm]);
        fputh(r, rfile[segm]);
        fputh(sh, sfile[segm]);
        fputh(sr, rfile[segm]);
    } else {
        sh = h;
        sr = r;
    }
    count[segm]++;
}

void align(int s)
{
    register short save;

    if (s != segm) {
        save = segm;
        segm = s;
    } else
        save = -1;

    if (count[s] & 01)
        puthr(s == STEXT ? EMPCOM : 0L, (long)RABS);

    if (save >= 0)
        segm = save;
}

static long enterconst(int bs)
{
    register short hash, i;
    register long h, h2, hr2;

    h   = intval.left;
    h2  = intval.right;
    hr2 = SEGMREL(bs);
    if (bs == SEXT)
        hr2 |= RPUTIX(extref);
    hash = SUPERHASH(h + h2 + hr2, HCONSZ - 1);
    while ((i = hashconst[hash]) != -1) {
        if (h == constab[i].h && h2 == constab[i].h2 && hr2 == constab[i].hr2)
            return i;
        if (--hash < 0)
            hash += HCONSZ;
    }
    hashconst[hash]     = nconst;
    constab[nconst].h   = h;
    constab[nconst].h2  = h2;
    constab[nconst].hr2 = hr2;
    return nconst++;
}

static void makecmd(long val, int type)
{
    register short clex, index, incr;
    register long addr, reltype;
    int cval, segment;

    index   = regleft;
    reltype = RABS;
    for (;;) {
        switch (clex = getlex(&cval)) {
        case LEOF:
        case LEOL:
            ungetlex(clex, cval);
            addr = 0;
            goto putcom;
        case '#':
            getexpr(&segment);
            if (type & TLIT) {
                addr = intval.right >> 19 & 017777;
                if (type & TINT) {
                    if ((!addr && !intval.left && !(intval.left >> 16)) ||
                        (addr == 017777 && intval.left == 0xfffff)) {
                        addr = intval.right & 0xfffff;
                        val |= 0x4000000;
                        reltype = SEGMREL(segment);
                        if (reltype == REXT)
                            reltype |= RPUTIX(extref);
                        break;
                    }
                } else {
                    if ((!addr && !intval.left && !(intval.left >> 16)) ||
                        (addr == 017777 && intval.left == 0xffffffff)) {
                        addr = intval.right & 0xfffff;
                        val |= 0x4000000;
                        reltype = SEGMREL(segment);
                        if (reltype == REXT)
                            reltype |= RPUTIX(extref);
                        break;
                    }
                }
            }
            addr    = enterconst(segment);
            reltype = RCONST;
            break;
        case '[':
            makecmd(WTCCOM, TLONG);
            if (getlex(&cval) != ']')
                uerror("bad [] syntax");
            continue;
        case '<':
            makecmd(UTCCOM, TLONG);
            if (getlex(&cval) != '>')
                uerror("bad <> syntax");
            continue;
        default:
            ungetlex(clex, cval);
            addr    = getexpr(&segment);
            reltype = SEGMREL(segment);
            if (reltype == REXT)
                reltype |= RPUTIX(extref);
            break;
        }
        break;
    }
    if ((clex = getlex(&cval)) == ',') {
        index = getexpr(&segment);
        if (segment != SABS)
            uerror("bad register number");
        if ((type & TCOMP) && addr == 0 && reltype == RABS) {
            if ((clex = getlex(&cval)) == LINCR || clex == LDECR) {
                incr = getexpr(&segment);
                if (segment != SABS)
                    uerror("bad register increment");
                if (incr == 0)
                    incr = 1;
                // make a component instruction
                addr = clex == LINCR ? incr : -incr;
                val  = MAKECOMP(val);
            } else
                ungetlex(clex, cval);
        }
    } else
        ungetlex(clex, cval);
putcom:
    if (type & TLONG) {
        if ((reltype & REXT) == REXT && stab[RGETIX(reltype)].n_type == N_EXT + N_ACOMM) {
            // if the instruction references ACOMM,
            // insert utc before it
            puthr((long)index << 28 | UTCCOM | (addr >> 12 & 0xfffff), reltype | RSHIFT);
            puthr(val | (addr & 07777), (long)RABS | RTRUNC);
        } else {
            addr &= 0xfffff;
            puthr((long)index << 28 | val | (addr & 0xfffff), reltype | RLONG);
        }
    } else {
        puthr((long)index << 28 | val | (addr & 07777), reltype | RSHORT);
    }
    if (!aflag && (type & TALIGN))
        align(segm);
}

static void makeascii(void)
{
    register short c, n;
    int cval;

    c = getlex(&cval);
    if (c != '"')
        uerror("no .ascii parameter");
    n = 0;
    for (;;) {
        switch (c = getchar()) {
        case EOF:
            uerror("EOF in text string");
        case '"':
            break;
        case '\\':
            switch (c = getchar()) {
            case EOF:
                uerror("EOF in text string");
            case '\n':
                continue;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                cval = c & 07;
                c    = getchar();
                if (c >= '0' && c <= '7') {
                    cval = cval << 3 | (c & 7);
                    c    = getchar();
                    if (c >= '0' && c <= '7') {
                        cval = cval << 3 | (c & 7);
                    } else
                        ungetc(c, stdin);
                } else
                    ungetc(c, stdin);
                c = cval;
                break;
            case 't':
                c = '\t';
                break;
            case 'b':
                c = '\b';
                break;
            case 'r':
                c = '\r';
                break;
            case 'n':
                c = '\n';
                break;
            case 'f':
                c = '\f';
                break;
            }
        default:
            fputc(c, sfile[segm]);
            n++;
            continue;
        }
        break;
    }
    c = W - n % W;
    n = (n + c) / (W / 2);
    count[segm] += n;
    while (c--)
        fputc(0, sfile[segm]);
    while (n--)
        fputh(0L, rfile[segm]);
}

void pass1(void)
{
    register short clex;
    int cval, tval, csegm;
    register long addr;

    segm = STEXT;
    while ((clex = getlex(&cval)) != LEOF) {
        switch (clex) {
        case LEOF:
            return;
        case LEOL:
            regleft = 0;
            continue;
        case ':':
            align(segm);
            continue;
        case LNUM:
            ungetlex(clex, cval);
            getexpr(&cval);
            if (cval != SABS)
                uerror("bad register number");
            regleft = intval.right & 017;
            continue;
        case LCMD:
            makecmd(table[cval].val, table[cval].type);
            break;
        case LSCMD:
            makecmd((long)cval << 12 | 0x3f00000L, 0);
            break;
        case LLCMD:
            makecmd((long)cval << 20, TLONG);
            break;
        case '.':
            if (getlex(&cval) != '=')
                uerror("bad command");
            align(segm);
            addr = 2 * getexpr(&csegm);
            if (csegm != segm)
                uerror("bad count assignment");
            if (addr < count[segm])
                uerror("negative count increment");
            if (segm == SBSS)
                count[segm] = addr;
            else
                while (count[segm] < addr) {
                    fputh(segm == STEXT ? EMPCOM : 0L, sfile[segm]);
                    fputh(0L, rfile[segm]);
                    count[segm]++;
                }
            break;
        case LNAME:
            if ((clex = getlex(&tval)) == ':') {
                align(segm);
                stab[cval].n_value = count[segm] / 2;
                stab[cval].n_type &= ~N_TYPE;
                stab[cval].n_type |= SEGMTYPE(segm);
                continue;
            } else if (clex == '=' || (clex == LACMD && tval == EQU)) {
                stab[cval].n_value = getexpr(&csegm);
                if (csegm == SEXT)
                    uerror("indirect equivalence");
                stab[cval].n_type &= N_EXT;
                stab[cval].n_type |= SEGMTYPE(csegm);
                break;
            } else if (clex == LACMD && (tval == COMM || tval == ACOMM)) {
                // name .comm len
                if (stab[cval].n_type != N_UNDF && stab[cval].n_type != (N_EXT | N_COMM) &&
                    stab[cval].n_type != (N_EXT | N_ACOMM))
                    uerror("name already defined");
                stab[cval].n_type = N_EXT | (tval == COMM ? N_COMM : N_ACOMM);
                getexpr(&tval);
                if (tval != SABS)
                    uerror("bad length .comm");
                stab[cval].n_value = intval.right;
                break;
            }
            uerror("bad command");
        case LACMD:
            switch (cval) {
            case TEXT:
                segm = STEXT;
                break;
            case DATA:
                segm = SDATA;
                break;
            case STRNG:
                segm = SSTRNG;
                break;
            case BSS:
                segm = SBSS;
                break;
            case HALF:
                for (;;) {
                    getexpr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(extref);
                    puthr(intval.right, addr);
                    if ((clex = getlex(&cval)) != ',') {
                        ungetlex(clex, cval);
                        break;
                    }
                }
                break;
            case WORD:
                align(segm);
                for (;;) {
                    getexpr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(extref);
                    fputh(intval.right, sfile[segm]);
                    fputh(addr, rfile[segm]);
                    fputh(intval.left, sfile[segm]);
                    fputh(0L, rfile[segm]);
                    count[segm] += 2;
                    if ((clex = getlex(&cval)) != ',') {
                        ungetlex(clex, cval);
                        break;
                    }
                }
                break;
            case ASCII:
                align(segm);
                makeascii();
                break;
            case GLOBL:
                for (;;) {
                    if ((clex = getlex(&cval)) != LNAME)
                        uerror("bad parameter .globl");
                    stab[cval].n_type |= N_EXT;
                    if ((clex = getlex(&cval)) != ',') {
                        ungetlex(clex, cval);
                        break;
                    }
                }
                break;
            case COMM:
            case ACOMM:
                // .comm name,len
                tval = cval;
                if (getlex(&cval) != LNAME)
                    uerror("bad parameter .comm");
                if (stab[cval].n_type != N_UNDF && stab[cval].n_type != (N_EXT | N_COMM) &&
                    stab[cval].n_type != (N_EXT | N_ACOMM))
                    uerror("name already defined");
                stab[cval].n_type = N_EXT | (tval == COMM ? N_COMM : N_ACOMM);
                if ((clex = getlex(&tval)) == ',') {
                    getexpr(&tval);
                    if (tval != SABS)
                        uerror("bad length .comm");
                } else {
                    ungetlex(clex, cval);
                    intval.right = 1;
                }
                stab[cval].n_value = intval.right;
                break;
            }
            break;
        default:
            uerror("bad syntax");
        }
        if ((clex = getlex(&cval)) != LEOL) {
            if (clex == LEOF)
                return;
            else
                uerror("bad command end");
        }
        regleft = 0;
    }
}
