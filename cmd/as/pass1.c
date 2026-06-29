//
// Assembler for BESM-6.
// First pass: code generation into temporary segment files.
//
#include <stdio.h>

#include "as.h"

static void puthr(long h, long r)
{
    static long sh, sr;

    if (as.count[as.segm] & 01) {
        fputh(sh, as.sfile[as.segm]); // first-of-pair  -> high half-word
        fputh(sr, as.rfile[as.segm]);
        fputh(h, as.sfile[as.segm]); // second-of-pair -> low half-word
        fputh(r, as.rfile[as.segm]);
    } else {
        sh = h;
        sr = r;
    }
    as.count[as.segm]++;
}

void align(int s)
{
    int save;

    if (s != as.segm) {
        save = as.segm;
        as.segm = s;
    } else
        save = -1;

    if (as.count[s] & 01)
        puthr(s == STEXT ? EMPCOM : 0L, (long)RABS);

    if (save >= 0)
        as.segm = save;
}

static long enterconst(int bs)
{
    int hash, i;
    long h, h2, hr2;

    h   = as.intval.left;
    h2  = as.intval.right;
    hr2 = SEGMREL(bs);
    if (bs == SEXT)
        hr2 |= RPUTIX(as.extref);
    hash = SUPERHASH(h + h2 + hr2, HCONSZ - 1);
    while ((i = as.hashconst[hash]) != -1) {
        if (h == as.constab[i].h && h2 == as.constab[i].h2 && hr2 == as.constab[i].hr2)
            return i;
        if (--hash < 0)
            hash += HCONSZ;
    }
    as.hashconst[hash]     = as.nconst;
    as.constab[as.nconst].h   = h;
    as.constab[as.nconst].h2  = h2;
    as.constab[as.nconst].hr2 = hr2;
    return as.nconst++;
}

static void makecmd(long val, int type)
{
    int clex, index;
    long addr, reltype;
    int cval, segment;

    index   = as.regleft;
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
                reltype |= RPUTIX(as.extref);
            break;
        }
        break;
    }
    if ((clex = getlex(&cval)) == ',') {
        index = getexpr(&segment);
        if (segment != SABS)
            uerror("bad register number");
    } else
        ungetlex(clex, cval);
putcom:
    if (type & TLONG) {
        if ((reltype & REXT) == REXT && as.stab[RGETIX(reltype)].n_type == N_EXT + N_ACOMM) {
            // if the instruction references ACOMM,
            // insert utc before it
            puthr((long)index << 20 | UTCCOM | (addr >> 12 & 077777), reltype | RSHIFT);
            puthr(val | (addr & 07777), (long)RABS | RTRUNC);
        } else {
            addr &= 077777;
            puthr((long)index << 20 | val | (addr & 077777), reltype | RLONG);
        }
    } else {
        puthr((long)index << 20 | val | (addr & 07777), reltype | RSHORT);
    }
    if (!as.aflag && (type & TALIGN))
        align(as.segm);
}

static void makeascii(void)
{
    int c, n;
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
            fputc(c, as.sfile[as.segm]);
            n++;
            continue;
        }
        break;
    }
    // Always append one trailing NUL byte, then zero-pad to a word boundary
    // (NUL-terminated string). When n is already a multiple of W this is a
    // full extra zero word: 1 NUL + (W-1) alignment bytes — intentional.
    c = W - n % W;
    n = (n + c) / (W / 2);
    as.count[as.segm] += n;
    while (c--)
        fputc(0, as.sfile[as.segm]);
    while (n--)
        fputh(0L, as.rfile[as.segm]);
}

void pass1(void)
{
    int cval, tval, csegm;
    long addr;

    as.segm = STEXT;
    for (;;) {
        int clex;

        // A machine instruction is expected at the start of a statement;
        // enable operator characters in names for this lex only.
        as.cmdmode = 1;
        clex    = getlex(&cval);
        as.cmdmode = 0;
        switch (clex) {
        case LEOF:
            return;
        case LEOL:
            as.regleft = 0;
            continue;
        case ':':
            align(as.segm);
            continue;
        case LNUM:
            ungetlex(clex, cval);
            as.cmdmode = 1; // a mnemonic may follow the register number
            getexpr(&cval);
            as.cmdmode = 0;
            if (cval != SABS)
                uerror("bad register number");
            as.regleft = as.intval.right & 017;
            continue;
        case LCMD:
            makecmd(table[cval].val, table[cval].type);
            break;
        case LSCMD:
            makecmd((long)cval << 12, 0);
            break;
        case LLCMD:
            makecmd((long)cval << 15, TLONG);
            break;
        case '.':
            if (getlex(&cval) != '=')
                uerror("bad command");
            align(as.segm);
            addr = 2 * getexpr(&csegm);
            if (csegm != as.segm)
                uerror("bad count assignment");
            if (addr < as.count[as.segm])
                uerror("negative count increment");
            if (as.segm == SBSS)
                as.count[as.segm] = addr;
            else
                while (as.count[as.segm] < addr) {
                    fputh(as.segm == STEXT ? EMPCOM : 0L, as.sfile[as.segm]);
                    fputh(0L, as.rfile[as.segm]);
                    as.count[as.segm]++;
                }
            break;
        case LNAME:
            if ((clex = getlex(&tval)) == ':') {
                align(as.segm);
                as.stab[cval].n_value = as.count[as.segm] / 2;
                as.stab[cval].n_type &= ~N_TYPE;
                as.stab[cval].n_type |= SEGMTYPE(as.segm);
                continue;
            } else if (clex == '=' || (clex == LACMD && tval == EQU)) {
                as.stab[cval].n_value = getexpr(&csegm);
                if (csegm == SEXT)
                    uerror("indirect equivalence");
                as.stab[cval].n_type &= N_EXT;
                as.stab[cval].n_type |= SEGMTYPE(csegm);
                break;
            } else if (clex == LACMD && (tval == COMM || tval == ACOMM)) {
                // name .comm len
                if (as.stab[cval].n_type != N_UNDF && as.stab[cval].n_type != (N_EXT | N_COMM) &&
                    as.stab[cval].n_type != (N_EXT | N_ACOMM))
                    uerror("name already defined");
                as.stab[cval].n_type = N_EXT | (tval == COMM ? N_COMM : N_ACOMM);
                getexpr(&tval);
                if (tval != SABS)
                    uerror("bad length .comm");
                as.stab[cval].n_value = as.intval.right;
                break;
            }
            uerror("bad command");
        case LACMD:
            switch (cval) {
            case TEXT:
                as.segm = STEXT;
                break;
            case DATA:
                as.segm = SDATA;
                break;
            case STRNG:
                as.segm = SSTRNG;
                break;
            case BSS:
                as.segm = SBSS;
                break;
            case HALF:
                for (;;) {
                    getexpr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(as.extref);
                    puthr(as.intval.right, addr);
                    if ((clex = getlex(&cval)) != ',') {
                        ungetlex(clex, cval);
                        break;
                    }
                }
                break;
            case WORD:
                align(as.segm);
                for (;;) {
                    getexpr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(as.extref);
                    fputh(as.intval.right, as.sfile[as.segm]);
                    fputh(addr, as.rfile[as.segm]);
                    fputh(as.intval.left, as.sfile[as.segm]);
                    fputh(0L, as.rfile[as.segm]);
                    as.count[as.segm] += 2;
                    if ((clex = getlex(&cval)) != ',') {
                        ungetlex(clex, cval);
                        break;
                    }
                }
                break;
            case ASCII:
                align(as.segm);
                makeascii();
                break;
            case GLOBL:
                for (;;) {
                    if ((clex = getlex(&cval)) != LNAME)
                        uerror("bad parameter .globl");
                    as.stab[cval].n_type |= N_EXT;
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
                if (as.stab[cval].n_type != N_UNDF && as.stab[cval].n_type != (N_EXT | N_COMM) &&
                    as.stab[cval].n_type != (N_EXT | N_ACOMM))
                    uerror("name already defined");
                as.stab[cval].n_type = N_EXT | (tval == COMM ? N_COMM : N_ACOMM);
                if ((clex = getlex(&tval)) == ',') {
                    getexpr(&tval);
                    if (tval != SABS)
                        uerror("bad length .comm");
                } else {
                    ungetlex(clex, cval);
                    as.intval.right = 1;
                }
                as.stab[cval].n_value = as.intval.right;
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
        as.regleft = 0;
    }
}
