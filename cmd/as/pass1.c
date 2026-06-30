//
// Assembler for BESM-6.
// First pass: translate the source into machine half-words and write them into
// the current segment's temp files (the code image plus a parallel relocation
// stream).  Addresses that are not yet known are emitted with a relocation
// record so pass 2 can patch them once the segment layout is fixed.
//
#include <stdio.h>

#include "as.h"

//
// Emit one 24-bit half-word `h` and its relocation half-word `r` into the
// current segment.  A BESM-6 word holds two half-words, written together, so
// this buffers the first half-word of a pair (in the statics sh/sr) and flushes
// both only when the second arrives.  count[] tracks how many half-words the
// segment holds, and its low bit tells us which half of the pair we are on.
//
static void emit_halfword(long h, long r)
{
    static long sh, sr;

    if (as.count[as.segm] & 01) {
        fputh(sh, as.sfile[as.segm]); // first-of-pair  -> high half-word
        fputh(sr, as.rfile[as.segm]);
        fputh(h, as.sfile[as.segm]); // second-of-pair -> low half-word
        fputh(r, as.rfile[as.segm]);
    } else {
        // Odd half-word: hold it until its partner shows up.
        sh = h;
        sr = r;
    }
    as.count[as.segm]++;
}

//
// Make sure segment `s` ends on a whole-word boundary: if it currently holds an
// odd number of half-words, emit one filler half-word (an empty instruction in
// text, a zero elsewhere).  Switches to segment s if needed and restores the
// previous segment afterwards.
//
void align_segment(int s)
{
    int save;

    if (s != as.segm) {
        save = as.segm;
        as.segm = s;
    } else
        save = -1;

    if (as.count[s] & 01)
        emit_halfword(s == STEXT ? EMPCOM : 0L, (long)RABS);

    if (save >= 0)
        as.segm = save;
}

//
// Add the value currently in as.intval to the constant pool and return its
// index there.  Identical constants are stored only once: the pool is a hash
// table, and a constant is keyed on both its 48-bit value and its relocation
// type `bs` (so e.g. an absolute 5 and an address-of-something that happens to
// equal 5 stay distinct).  Collisions are resolved by linear probing - on a
// clash, step to the previous slot, wrapping around - the same scheme used by
// the symbol and instruction tables.
//
static long intern_constant(int bs)
{
    int hash, i;
    long h, h2, hr2;

    h   = as.intval.left;
    h2  = as.intval.right;
    hr2 = SEGMREL(bs);
    if (bs == SEXT)
        hr2 |= RPUTIX(as.extref); // external constant: remember which symbol
    hash = SUPERHASH(h + h2 + hr2, HCONSZ - 1);
    while ((i = as.hashconst[hash]) != -1) {
        // Slot taken: a real match means the constant already exists.
        if (h == as.constab[i].h && h2 == as.constab[i].h2 && hr2 == as.constab[i].hr2)
            return i;
        if (--hash < 0)
            hash += HCONSZ;
    }
    // Not found: append a new pool entry and record it in the hash slot.
    as.hashconst[hash]     = as.nconst;
    as.constab[as.nconst].h   = h;
    as.constab[as.nconst].h2  = h2;
    as.constab[as.nconst].hr2 = hr2;
    return as.nconst++;
}

//
// Assemble one machine instruction and emit it.  `val` is the base opcode word
// (from table[] or a raw $NN/@NN), `type` carries the TLONG/TALIGN flags.  The
// job is to parse the operand and turn it into the instruction's address field
// plus a relocation record:
//   * "# expr"  - the operand is a constant; intern it and point at the pool.
//   * "[ ... ]" / "< ... >" - shorthand that emits a wtc/utc instruction first
//                 (the BESM-6 way of forming a long address), then continues.
//   * otherwise - the operand is an address expression.
// An optional ", reg" sets the index register field.  Finally the half-word(s)
// are emitted at the `putcom` label, choosing the long/short encoding (and the
// special two-word form when the target is an absolute-common symbol).
//
static void assemble_instruction(long val, int type)
{
    int clex, index;
    long addr, reltype;
    int cval, segment;

    index   = as.regleft; // index register set by a "N M" prefix, if any
    reltype = RABS;
    for (;;) {
        switch (clex = next_token(&cval)) {
        case LEOF:
        case LEOL:
            // No operand at all: address field is zero.
            unget_token(clex, cval);
            addr = 0;
            goto putcom;
        case '#':
            // "# expr": immediate constant, placed in the constant pool; the
            // address field becomes the pool index, relocated as RCONST.
            parse_expr(&segment);
            addr    = intern_constant(segment);
            reltype = RCONST;
            break;
        case '[':
            // "[ ... ]" expands to a wtc instruction before this one.
            assemble_instruction(WTCCOM, TLONG);
            if (next_token(&cval) != ']')
                fatal("bad [] syntax");
            continue;
        case '<':
            // "< ... >" expands to a utc instruction before this one.
            assemble_instruction(UTCCOM, TLONG);
            if (next_token(&cval) != '>')
                fatal("bad <> syntax");
            continue;
        default:
            // An ordinary address expression; its segment fixes the relocation
            // type, and an external reference also packs in the symbol index.
            unget_token(clex, cval);
            addr    = parse_expr(&segment);
            reltype = SEGMREL(segment);
            if (reltype == REXT)
                reltype |= RPUTIX(as.extref);
            break;
        }
        break;
    }
    // Optional ", reg": an index register number appended to the operand.
    if ((clex = next_token(&cval)) == ',') {
        index = parse_expr(&segment);
        if (segment != SABS)
            fatal("bad register number");
    } else
        unget_token(clex, cval);
putcom:
    // Build the final half-word(s): OR together the index register (bits
    // 20..23), the opcode, and the address field, and attach the relocation.
    if (type & TLONG) {
        if ((reltype & REXT) == REXT && as.stab[RGETIX(reltype)].n_type == N_EXT + N_ACOMM) {
            // A long instruction referencing an absolute-common symbol needs a
            // utc in front to carry the high address bits, so emit two
            // half-words: the utc (high 15 bits, RSHIFT) then the instruction
            // itself (low 12 bits, RTRUNC).
            emit_halfword((long)index << 20 | UTCCOM | (addr >> 12 & 077777), reltype | RSHIFT);
            emit_halfword(val | (addr & 07777), (long)RABS | RTRUNC);
        } else {
            // Normal long instruction: a 15-bit address field.
            addr &= 077777;
            emit_halfword((long)index << 20 | val | (addr & 077777), reltype | RLONG);
        }
    } else {
        // Short instruction: a 12-bit address field.
        emit_halfword((long)index << 20 | val | (addr & 07777), reltype | RSHORT);
    }
    if (!as.aflag && (type & TALIGN))
        align_segment(as.segm);
}

//
// Assemble a ".ascii \"...\"" string constant.  The bytes between the quotes
// are written directly into the segment image (six characters pack into one
// 48-bit word).  C-style backslash escapes are understood: \n \t \b \r \f, an
// octal \NNN (1-3 digits), and a backslash-newline line continuation.  The
// string is then NUL-terminated and zero-padded up to a whole word.
//
static void assemble_ascii(void)
{
    int c, n;
    int cval;

    c = next_token(&cval);
    if (c != '"')
        fatal("no .ascii parameter");
    n = 0; // count of characters emitted so far
    for (;;) {
        switch (c = getchar()) {
        case EOF:
            fatal("EOF in text string");
        case '"':
            break; // closing quote ends the string
        case '\\':
            // Escape sequence: decode the character that follows.
            switch (c = getchar()) {
            case EOF:
                fatal("EOF in text string");
            case '\n':
                continue; // backslash-newline: line continuation, emit nothing
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                // Octal escape \N, \NN or \NNN: gather up to three octal digits.
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
    c = W - n % W;            // bytes of padding to add (1..W)
    n = (n + c) / (W / 2);    // total half-words now written
    as.count[as.segm] += n;
    while (c--)
        fputc(0, as.sfile[as.segm]); // zero-pad the code image
    while (n--)
        fputh(0L, as.rfile[as.segm]); // matching (absolute) relocation half-words
}

//
// Pass 1's main loop: read the source one statement at a time and act on it.
// A statement is one of: a label "name:", a location-counter assignment ".=",
// a name definition ("name = expr" or ".equ"), a ".comm/.acomm" declaration, a
// machine instruction (named or raw $NN/@NN), an "N M" index-register prefix,
// or an assembler directive (.text, .data, .word, .ascii, .globl, ...).  It
// runs until end of file.  cmdmode is turned on around the leading token so
// that mnemonics containing '+ - * /' scan as one name.
//
void generate_code(void)
{
    int cval, tval, csegm;
    long addr;

    as.segm = STEXT; // assembling starts in the text segment
    for (;;) {
        int clex;

        // A machine instruction is expected at the start of a statement;
        // enable operator characters in names for this lex only.
        as.cmdmode = 1;
        clex    = next_token(&cval);
        as.cmdmode = 0;
        switch (clex) {
        case LEOF:
            return;
        case LEOL:
            as.regleft = 0; // end of statement: forget any index-register prefix
            continue;
        case ':':
            align_segment(as.segm); // a bare ":" forces word alignment
            continue;
        case LNUM:
            // A leading number is an "N M" prefix: the index register written
            // to the left of the next instruction.
            unget_token(clex, cval);
            as.cmdmode = 1; // a mnemonic may follow the register number
            parse_expr(&cval);
            as.cmdmode = 0;
            if (cval != SABS)
                fatal("bad register number");
            as.regleft = as.intval.right & 017;
            continue;
        case LCMD:
            // A named instruction: assemble with its table entry.
            assemble_instruction(table[cval].val, table[cval].type);
            break;
        case LSCMD:
            // Raw short opcode "$NN": opcode in bits 12+.
            assemble_instruction((long)cval << 12, 0);
            break;
        case LLCMD:
            // Raw long opcode "@NN": opcode in bits 15+.
            assemble_instruction((long)cval << 15, TLONG);
            break;
        case '.':
            // ".= expr" sets the location counter forward, padding the segment
            // with empty instructions / zeros (or, in bss, just reserving the
            // space).  It can only move forward within the current segment.
            if (next_token(&cval) != '=')
                fatal("bad command");
            align_segment(as.segm);
            addr = 2 * parse_expr(&csegm); // expr is in words; count[] in half-words
            if (csegm != as.segm)
                fatal("bad count assignment");
            if (addr < as.count[as.segm])
                fatal("negative count increment");
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
            // A name at statement start; what follows decides its meaning.
            if ((clex = next_token(&tval)) == ':') {
                // "name:" - a label: bind the name to the current location.
                align_segment(as.segm);
                as.stab[cval].n_value = as.count[as.segm] / 2;
                as.stab[cval].n_type &= ~N_TYPE;
                as.stab[cval].n_type |= SEGMTYPE(as.segm);
                continue;
            } else if (clex == '=' || (clex == LACMD && tval == EQU)) {
                // "name = expr" / "name .equ expr" - define a constant name.
                as.stab[cval].n_value = parse_expr(&csegm);
                if (csegm == SEXT)
                    fatal("indirect equivalence");
                as.stab[cval].n_type &= N_EXT;
                as.stab[cval].n_type |= SEGMTYPE(csegm);
                break;
            } else if (clex == LACMD && (tval == COMM || tval == ACOMM)) {
                // "name .comm len" - declare a common block of `len`.
                if (as.stab[cval].n_type != N_UNDF && as.stab[cval].n_type != (N_EXT | N_COMM) &&
                    as.stab[cval].n_type != (N_EXT | N_ACOMM))
                    fatal("name already defined");
                as.stab[cval].n_type = N_EXT | (tval == COMM ? N_COMM : N_ACOMM);
                parse_expr(&tval);
                if (tval != SABS)
                    fatal("bad length .comm");
                as.stab[cval].n_value = as.intval.right;
                break;
            }
            fatal("bad command");
        case LACMD:
            // An assembler directive.  Most either switch the active segment or
            // emit data into it.
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
                // ".half e, e, ..." - emit each value as one 24-bit half-word.
                for (;;) {
                    parse_expr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(as.extref);
                    emit_halfword(as.intval.right, addr);
                    if ((clex = next_token(&cval)) != ',') {
                        unget_token(clex, cval);
                        break;
                    }
                }
                break;
            case WORD:
                // ".word e, e, ..." - emit each value as one full 48-bit word
                // (low half carries the relocation, high half is absolute).
                align_segment(as.segm);
                for (;;) {
                    parse_expr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(as.extref);
                    fputh(as.intval.right, as.sfile[as.segm]);
                    fputh(addr, as.rfile[as.segm]);
                    fputh(as.intval.left, as.sfile[as.segm]);
                    fputh(0L, as.rfile[as.segm]);
                    as.count[as.segm] += 2;
                    if ((clex = next_token(&cval)) != ',') {
                        unget_token(clex, cval);
                        break;
                    }
                }
                break;
            case ASCII:
                align_segment(as.segm);
                assemble_ascii();
                break;
            case GLOBL:
                // ".globl a, b, ..." - mark each name as external (global).
                for (;;) {
                    if ((clex = next_token(&cval)) != LNAME)
                        fatal("bad parameter .globl");
                    as.stab[cval].n_type |= N_EXT;
                    if ((clex = next_token(&cval)) != ',') {
                        unget_token(clex, cval);
                        break;
                    }
                }
                break;
            case COMM:
            case ACOMM:
                // ".comm name, len" - the directive form of a common block.
                tval = cval;
                if (next_token(&cval) != LNAME)
                    fatal("bad parameter .comm");
                if (as.stab[cval].n_type != N_UNDF && as.stab[cval].n_type != (N_EXT | N_COMM) &&
                    as.stab[cval].n_type != (N_EXT | N_ACOMM))
                    fatal("name already defined");
                as.stab[cval].n_type = N_EXT | (tval == COMM ? N_COMM : N_ACOMM);
                if ((clex = next_token(&tval)) == ',') {
                    parse_expr(&tval);
                    if (tval != SABS)
                        fatal("bad length .comm");
                } else {
                    unget_token(clex, cval);
                    as.intval.right = 1;
                }
                as.stab[cval].n_value = as.intval.right;
                break;
            }
            break;
        default:
            fatal("bad syntax");
        }
        // After a statement that is not a label/prefix, the only thing allowed
        // is the end of the line (or the file).
        if ((clex = next_token(&cval)) != LEOL) {
            if (clex == LEOF)
                return;
            else
                fatal("bad command end");
        }
        as.regleft = 0; // the index-register prefix does not carry to the next line
    }
}
