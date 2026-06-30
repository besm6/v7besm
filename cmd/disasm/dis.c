/*
 * Disassembler for BESM-6 a.out object files.
 *
 * A BESM-6 word is 48 bits == 6 bytes (two 24-bit half-words).  Every text word
 * holds two 24-bit instructions; the high (left) half-word executes first.  The
 * instruction encoding and the mnemonic tables below match tmp/objdump.c and the
 * encoding emitted by cmd/as (see cmd/as/tables.c, doc/Besm6_Instruction_Set.md).
 */
#include <stdio.h>

#include "besm6/b.out.h"
#include "disasm.h"

#define W 6 /* длина слова в байтах */

struct exec hdr; /* заголовок */
FILE *text, *rel;
int rflag, Rflag, cflag, Cflag;
int addr;

/*
 * Long-address instructions (format flag = 1): opcodes 020-037, 16 entries.
 *
 * The MADLEN (ASCII) names match the assembler's table[] in cmd/as/tables.c;
 * opcodes the assembler leaves unnamed disassemble to its raw long-opcode form
 * "@NN" (octal), which the assembler accepts back verbatim.  The BEMSH set is
 * the Cyrillic dialect from tmp/objdump.c.
 */
const char *lcmd_bemsh[16] = {
    "э20", "э21", "мода", "мод", "уиа", "слиа", "по",   "пе",
    "пб",  "пв",  "выпр", "стоп", "пио", "пино", "э36",  "цикл",
};

const char *lcmd_madlen[16] = {
    "@20", "@21", "utc",  "wtc", "vtm", "utm", "uza",  "u1a",
    "uj",  "vjm", "ij",   "stop", "vzm", "v1m", "@36",  "vlm",
};

/*
 * Short-address instructions (format flag = 0): opcodes 000-077, 64 entries.
 * MADLEN names match cmd/as/tables.c; unnamed opcodes use the raw "$NN" form.
 */
const char *scmd_bemsh[64] = {
    "зп",   "зпм",  "рег",  "счм",  "сл",   "вч",   "вчоб", "вчаб",
    "сч",   "и",    "нтж",  "слц",  "знак", "или",  "дел",  "умн",
    "сбр",  "рзб",  "чед",  "нед",  "слп",  "вчп",  "сд",   "рж",
    "счрж", "счмр", "уис",  "счис", "слпа", "вчпа", "сда",  "ржа",
    "уи",   "уим",  "счи",  "счим", "уии",  "сли",  "э46",  "э47",
    "э50",  "э51",  "э52",  "э53",  "э54",  "э55",  "э56",  "э57",
    "э60",  "э61",  "э62",  "э63",  "э64",  "э65",  "э66",  "э67",
    "э70",  "э71",  "э72",  "э73",  "э74",  "э75",  "э76",  "э77",
};

const char *scmd_madlen[64] = {
    "atx",  "stx",  "mod",  "xts",  "a+x",  "a-x",  "x-a",  "amx",
    "xta",  "aax",  "aex",  "arx",  "avx",  "aox",  "a/x",  "a*x",
    "apx",  "aux",  "acx",  "anx",  "e+x",  "e-x",  "asx",  "xtr",
    "rte",  "yta",  "$32",  "$33",  "e+n",  "e-n",  "asn",  "ntr",
    "ati",  "sti",  "ita",  "its",  "mtj",  "j+m",  "$46",  "$47",
    "$50",  "$51",  "$52",  "$53",  "$54",  "$55",  "$56",  "$57",
    "$60",  "$61",  "$62",  "$63",  "$64",  "$65",  "$66",  "$67",
    "$70",  "$71",  "$72",  "$73",  "$74",  "$75",  "$76",  "$77",
};

const char **lcmd = lcmd_madlen, **scmd = scmd_madlen;

/*
 * Decode one 24-bit instruction into re-assemblable text:
 *      mnemonic [' ' addr] [', ' reg]
 * The address is printed in octal: a single digit (1-7) bare, larger values
 * with a leading 0 (%#o); the modifier/index register is decimal.  Both
 * operands are omitted when zero, except that a non-zero register forces the
 * (possibly zero) address to be printed too.
 */
void disasm_insn(unsigned insn, char *buf)
{
    unsigned op_ir    = (insn >> 20) & 017; /* modifier register, bits 20-23 */
    unsigned op_lflag = (insn >> 19) & 1;   /* long-address format flag, bit 19 */
    unsigned op_addr;
    const char *mnem;

    if (op_lflag) {
        unsigned op_lcmd = (insn >> 15) & 037; /* opcode 020-037 */
        op_addr = insn & 077777;               /* 15-bit address */
        mnem    = lcmd[op_lcmd - 020];
    } else {
        unsigned op_scmd = (insn >> 12) & 0177; /* opcode + extension bit */
        op_addr = insn & 07777;                 /* 12-bit address */
        mnem    = scmd[op_scmd & 077];
        if (op_scmd & 0100)
            op_addr |= 070000; /* short address extended to 15 bits */
    }

    buf += sprintf(buf, "%s", mnem);
    if (op_addr != 0 || op_ir != 0)
        buf += sprintf(buf, op_addr < 8 ? " %o" : " %#o", op_addr);
    if (op_ir != 0)
        sprintf(buf, ", %u", op_ir);
}

/*
 * Print one relocation record (-r) symbolically, or as raw numbers (-R).
 */
void prrel(long r)
{
    if (Rflag) {
        printf("%d %d", (int)(r & REXT) >> 3, (int)(r & RSHORT));
        if (RGETIX(r))
            printf(" %d", (int)RGETIX(r));
        return;
    }
    switch ((int)r & REXT) {
    default:     putchar('?'); break;
    case RCONST: putchar('c'); break;
    case RTEXT:  putchar('t'); break;
    case RDATA:  putchar('d'); break;
    case RBSS:   putchar('b'); break;
    case RABSS:  putchar('y'); break;
    case RABS:   putchar('a'); break;
    case REXT:   printf("%d", (int)RGETIX(r));
    }
    switch ((int)r & RSHORT) {
    case RSHORT: putchar('s'); break;
    case 0:      putchar('a'); break;
    default:     putchar('?'); break;
    }
}

/*
 * Dump n data words (const or data segment) as octal half-words.
 */
void prwords(int n)
{
    while (n--) {
        long hi = fgeth(text);
        long lo = fgeth(text);
        printf("%5o:\t%08lo %08lo", addr++, hi & 077777777L, lo & 077777777L);
        if (rflag) {
            long rhi = fgeth(rel);
            long rlo = fgeth(rel);
            putchar('\t');
            prrel(rhi);
            putchar(' ');
            prrel(rlo);
        }
        putchar('\n');
    }
}

/*
 * Print one decoded instruction half-word, optionally with its raw octal value.
 */
void prcmd(long c)
{
    if (!Cflag) {
        char buf[64];
        disasm_insn((unsigned)c, buf);
        printf("\t%s", buf);
    }
    if (cflag)
        printf("\t%08lo", c & 077777777L);
}

/*
 * Disassemble n text words, high half-word first.
 */
void prtext(int n)
{
    while (n--) {
        long hi = fgeth(text);
        long lo = fgeth(text);
        long rhi = 0, rlo = 0;
        if (rflag) {
            rhi = fgeth(rel);
            rlo = fgeth(rel);
        }
        printf("%5o:", addr++);
        prcmd(hi);
        if (rflag) {
            putchar('\t');
            prrel(rhi);
        }
        putchar('\n');
        printf("      ");
        prcmd(lo);
        if (rflag) {
            putchar('\t');
            prrel(rlo);
        }
        putchar('\n');
    }
}

/*
 * Walk the segments in on-disk order: const, text, data.
 */
void disfile(void)
{
    addr = HDRSZ / W;
    prwords((int)(hdr.a_const / W));
    putchar('\n');
    prtext((int)(hdr.a_text / W));
    putchar('\n');
    prwords((int)(hdr.a_data / W));
}

/*
 * Disassemble a whole a.out object file to stdout.  Returns 0 on success.
 */
int disassemble(const char *fname)
{
    if ((text = fopen(fname, "r")) == NULL) {
        fprintf(stderr, "dis: %s not found\n", fname);
        return 1;
    }
    if (!fgethdr(text, &hdr) || N_BADMAG(hdr)) {
        fprintf(stderr, "dis: %s not an object file\n", fname);
        fclose(text);
        return 1;
    }
    if (rflag) {
        if (!(hdr.a_flag & RELFLG)) {
            fprintf(stderr, "dis: %s is not relocatable\n", fname);
            fclose(text);
            return 1;
        }
        if ((rel = fopen(fname, "r")) == NULL) {
            fprintf(stderr, "dis: %s not found\n", fname);
            fclose(text);
            return 1;
        }
        fseek(rel, N_SYMOFF(hdr), 0);
    }
    disfile();
    if (rflag)
        fclose(rel);
    fclose(text);
    return 0;
}
