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
// Record a completed const-segment word in the dedup table, so that a later
// "#expr" can address it instead of appending its own copy.  `val` is the whole
// 48-bit word and `rel` the relocation of its value; `addr` is its offset in
// words from the start of the segment.
//
// Registration is best-effort: the table is a cache, not a limit.  Once it is
// full we simply stop recording, and identical constants start costing a word
// each - correct, only bigger.  Keeping the table below its hash size also
// guarantees the linear probing here and in intern_constant() finds a free slot.
//
static void register_constant(int64_t val, long rel, long addr)
{
    int hash, i;

    if (as.nconst >= CSIZE)
        return; // table full: give up de-duplicating, do not fail
    hash = SUPERHASH(HIHALF(val) + LOHALF(val) + rel, HCONSZ - 1);
    while ((i = as.hashconst[hash]) != -1) {
        if (val == as.constab[i].val && rel == as.constab[i].rel)
            return; // already known: keep the word we found first
        if (--hash < 0)
            hash += HCONSZ;
    }
    as.hashconst[hash]         = as.nconst;
    as.constab[as.nconst].val  = val;
    as.constab[as.nconst].rel  = rel;
    as.constab[as.nconst].addr = addr;
    as.nconst++;
}

//
// Emit one 24-bit half-word `h` and its relocation half-word `r` into the
// current segment.  A BESM-6 word holds two half-words, written together, so
// this buffers the first half-word of a pair (in as.pendh/pendr, one slot per
// segment) and flushes both only when the second arrives.  count[] tracks how
// many half-words the segment holds, and its low bit tells us which half of the
// pair we are on.
//
// Completing a word in the const segment also offers it to the dedup table, as
// long as neither half needs relocating - a word that moves at link time, or
// whose value is an address, is no use as a literal.  A word laid down by
// intern_constant() carries RMERGE in its high half and registers itself there,
// under the value's own relocation type; skipping it here keeps that key intact.
//
static void emit_halfword(long h, long r)
{
    if (as.count[as.segm] & 01) {
        long sh = as.pendh[as.segm];
        long sr = as.pendr[as.segm];

        fputh(sh, as.sfile[as.segm]); // first-of-pair  -> high half-word
        fputh(sr, as.rfile[as.segm]);
        fputh(h, as.sfile[as.segm]); // second-of-pair -> low half-word
        fputh(r, as.rfile[as.segm]);
        as.count[as.segm]++;

        if (as.segm == SCONST && !(sr & RMERGE) && (sr & REXT) == RABS && (r & REXT) == RABS)
            register_constant(((int64_t)sh << 24) | (h & HALF_MASK), RABS,
                              as.count[SCONST] / 2 - 1);
        return;
    }
    // Odd half-word: hold it until its partner shows up.
    as.pendh[as.segm] = h;
    as.pendr[as.segm] = r;
    as.count[as.segm]++;
}

//
// Make sure segment `s` ends on a whole-word boundary: if it currently holds an
// odd number of half-words, emit one filler half-word (an empty instruction in
// text, a zero everywhere else).  Switches to segment s if needed and restores
// the previous segment afterwards.
//
// The const segment pads with zeros even though it may hold code, matching what
// ". = expr" and ".org" fill their gaps with: a zero half-word is `atx 0', and
// stores to address 0 are discarded (doc/Besm6_Instruction_Set.md), so it is as
// inert to fall through as the `utc 0' the text segment uses.
//
void align_segment(int s)
{
    int save;

    if (s != as.segm) {
        save    = as.segm;
        as.segm = s;
    } else
        save = -1;

    if (as.count[s] & 01)
        emit_halfword(s == STEXT ? EMPCOM : 0L, (long)RABS);

    if (save >= 0)
        as.segm = save;
}

//
// Move the current segment's location counter forward to `words`, its offset in
// words from the start of the segment, padding the gap with empty instructions in
// the text segment and zeros elsewhere; in bss nothing is emitted and the counter
// alone moves.  The counter only ever moves forward.
//
// The caller must have aligned the segment before parsing the operand, so that a
// self-referential expression like ". + 3" reads the same counter this lands on.
// Backs both ". = expr" and ".org addr".
//
static void set_location(long words)
{
    long half = 2 * words; // the operand is in words, count[] is in half-words

    if (half < as.count[as.segm])
        fatal("negative count increment");

    if (as.segm == SBSS) {
        as.count[as.segm] = half;
        return;
    }
    while (as.count[as.segm] < half) {
        fputh(as.segm == STEXT ? EMPCOM : 0L, as.sfile[as.segm]);
        fputh(0L, as.rfile[as.segm]);
        as.count[as.segm]++;
    }
}

//
// Find the const-segment word holding the 48-bit value `val`, appending one if
// no suitable word is there yet, and return its offset in words from the start
// of the segment.  Backs the "#expr" operand.
//
// A word only serves as `val` if it also agrees on the relocation: `bs` is the
// value's segment, so an absolute 5 and the address of something that happens
// to sit at 5 stay distinct, and `extref` names the symbol when `bs` is SEXT.
// The candidates are whatever register_constant() has offered - words appended
// here, and any word a ".const" directive laid down with both halves absolute.
// Collisions are resolved by linear probing, stepping to the previous slot and
// wrapping around, the same scheme the symbol and instruction tables use.
//
// A fresh word is appended at the const segment's cursor, wherever that is, and
// carries RMERGE in its high half to tell the linker it is an anonymous literal
// that may be merged with an identical one from another object file.
//
static long intern_constant(int64_t val, int bs, int extref)
{
    int hash, i, save;
    long hr2, addr;

    hr2 = SEGMREL(bs);
    if (bs == SEXT)
        hr2 |= RPUTIX(extref); // external constant: remember which symbol
    hash = SUPERHASH(HIHALF(val) + LOHALF(val) + hr2, HCONSZ - 1);
    while ((i = as.hashconst[hash]) != -1) {
        // Slot taken: a real match means a suitable word already exists.
        if (val == as.constab[i].val && hr2 == as.constab[i].rel)
            return as.constab[i].addr;
        if (--hash < 0)
            hash += HCONSZ;
    }
    // Not found: append the literal to the const segment.  align_segment() first,
    // since a word must start on a word boundary; the caller's segment keeps its
    // own half-finished word (as.pendh/pendr are per-segment).
    align_segment(SCONST);
    save    = as.segm;
    as.segm = SCONST;
    emit_halfword(HIHALF(val), (long)RMERGE);
    emit_halfword(LOHALF(val), hr2);
    as.segm = save;

    addr = as.count[SCONST] / 2 - 1;
    register_constant(val, hr2, addr);
    return addr;
}

//
// Assemble one machine instruction and emit it.  `val` is the base opcode word
// (from table[] or a raw $NN/@NN), `type` carries the TLONG/TALIGN flags.  The
// job is to parse the operand and turn it into the instruction's address field
// plus a relocation record:
//   * "# expr"  - the operand is a constant; point at the constant pool, or at
//                 memory word 0 when the constant is an absolute zero.
//   * "[ ... ]" / "< ... >" - shorthand that emits a wtc/utc instruction first
//                 (the BESM-6 way of forming a long address), then continues.
//   * otherwise - the operand is an address expression.
// `index` is the index register the caller wants in the modifier field: the
// "N M" prefix for a statement-level instruction, and 0 for the utc/wtc that
// the "<>"/"[]" expansion below generates.  An optional ", reg" overrides it.
// Finally the half-word(s) are emitted at the `putcom` label, choosing the
// long/short encoding (and the special two-word form when the target is an
// absolute-common symbol).
//
static void assemble_instruction(long val, int type, int index)
{
    int clex;
    long addr, reltype;
    int cval, segment;
    int pooled      = 0;    // a pending "# expr", awaiting the final index register
    int cset        = 0;    // a utc/wtc has loaded C, which modifies the next address
    int64_t poolval = 0;    // the "# expr" value / segment / external symbol, captured
    int poolseg     = SABS; //   before the ", reg" parse below clobbers as.intval
    int poolext     = 0;    //   and as.extref

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
            // "# expr": an immediate constant.  Interning is deferred until the
            // index register is known, because memory word 0 always reads as 0:
            // an unindexed absolute "#0" addresses 0 directly and needs no pool
            // slot.  Capture the value now - the ", reg" parse below reuses
            // as.intval and as.extref.
            parse_expr(&segment);
            poolval = as.intval;
            poolseg = segment;
            poolext = as.extref;
            pooled  = 1;
            addr    = 0;
            break;
        case '[':
            // "[ ... ]" expands to a wtc instruction before this one.  The wtc
            // is unindexed: an index register belongs to the instruction the
            // programmer wrote it against, and indexing the wtc too would add
            // M[i] into C on top of the M[i] the instruction itself applies.
            assemble_instruction(WTCCOM, TLONG, 0);
            if (next_token(&cval) != ']')
                fatal("bad [] syntax");
            cset = 1; // wtc loaded C: it is added to this instruction's address
            continue;
        case '<':
            // "< ... >" expands to a utc instruction before this one, unindexed
            // for the same reason as the wtc above.
            assemble_instruction(UTCCOM, TLONG, 0);
            if (next_token(&cval) != '>')
                fatal("bad <> syntax");
            cset = 1; // utc loaded C: it is added to this instruction's address
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

    // Resolve a pending "# expr" now that the index register is final.  Indexing
    // a literal is meaningless - it would modify the pool address - and with
    // M == 017 it would encode a stack pop, so reject it outright.  An absolute
    // zero with no live C register then addresses memory word 0, which always
    // reads as 0; anything else goes into the pool.
    if (pooled) {
        if (index != 0)
            fatal("index register on a constant operand");
        if (as.segm == SCONST)
            // The literal would be appended to the const segment at the cursor,
            // i.e. right in front of the instruction referencing it.  Put the
            // constant in the segment yourself and address it by name.
            fatal("constant operand inside the const segment");
        if (poolseg == SABS && poolval == 0 && !cset) {
            addr    = 0;
            reltype = RABS;
        } else {
            addr    = intern_constant(poolval, poolseg, poolext);
            reltype = RCONST;
        }
    }
putcom:
    // Build the final half-word(s): OR together the index register (bits
    // 20..23), the opcode, and the address field, and attach the relocation.
    if (type & TLONG) {
        // Long instruction: a 15-bit address field.
        addr &= 077777;
        emit_halfword((long)index << 20 | val | (addr & 077777), reltype);
    } else if (reltype == RABS) {
        // Short instruction, absolute address: this is the final value, so the
        // segment bit can be decided now.  The address space is 15 bits and the
        // hardware forms EA modulo 0100000, so reduce the expression the same
        // way: that is what turns a negative literal such as the -5 of
        // "atx -5, 7" into 077773, and a 48-bit mask into the address it names.
        // What survives must land in [0..07777] or [070000..077777] - the field
        // reaches nothing in between, and such code needs a "< expr >" escape.
        addr &= 077777;
        if (!short_addr_fits(addr))
            fatal("short address out of range: 0%lo", addr);
        // RABS is 0, so the record carries the width modifier alone.
        emit_halfword(short_addr_put((long)index << 20 | val, addr), RSHORT);
    } else {
        // Short instruction, relocatable address: `addr` is only an offset into
        // its segment, so the final address - and with it the segment bit - is
        // not known until pass 2 adds the base (or the linker does).  Leave the
        // bit clear for them to set, and require the offset to reach pass 2
        // intact: the 12-bit field is all the room there is to carry it, and
        // silently truncating here would defeat the range check downstream.
        if (addr & ~SHORTOFF)
            fatal("short address out of range: 0%lo", addr);
        emit_halfword((long)index << 20 | val | (addr & SHORTOFF), reltype | RSHORT);
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
    c = W - n % W;         // bytes of padding to add (1..W)
    n = (n + c) / (W / 2); // total half-words now written
    as.count[as.segm] += n;
    while (c--)
        fputc(0, as.sfile[as.segm]); // zero-pad the code image
    while (n--)
        fputh(0L, as.rfile[as.segm]); // matching (absolute) relocation half-words
}

//
// Pass 1's main loop: read the source one statement at a time and act on it.
// A statement is one of: a label "name:", a location-counter assignment ".=",
// a name definition ("name = expr"), a machine instruction (named or raw
// $NN/@NN), an "N M" index-register prefix, or an assembler directive
// (.text, .data, .word, .ascii, .globl, .equ, .comm, ...).  It
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
        clex       = next_token(&cval);
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
            as.regleft = as.intval & 017;
            continue;
        case LCMD:
            // A named instruction: assemble with its table entry.
            assemble_instruction(table[cval].val, table[cval].type, as.regleft);
            break;
        case LSCMD:
            // Raw short opcode "$NN": opcode in bits 12+.
            assemble_instruction((long)cval << 12, 0, as.regleft);
            break;
        case LLCMD:
            // Raw long opcode "@NN": opcode in bits 15+.
            assemble_instruction((long)cval << 15, TLONG, as.regleft);
            break;
        case '.':
            // ".= expr" sets the location counter forward, padding the segment
            // with empty instructions / zeros (or, in bss, just reserving the
            // space).  It can only move forward within the current segment.
            if (next_token(&cval) != '=')
                fatal("bad command");
            align_segment(as.segm);
            addr = parse_expr(&csegm);
            if (csegm != as.segm)
                fatal("bad count assignment");
            set_location(addr);
            break;
        case LNAME:
            // A name at statement start; what follows decides its meaning.
            if ((clex = next_token(&tval)) == ':') {
                // "name:" - a label: bind the name to the current location.
                if ((as.stab[cval].n_type & N_TYPE) != N_UNDF)
                    fatal("name already defined");
                align_segment(as.segm);
                as.stab[cval].n_value = as.count[as.segm] / 2;
                as.stab[cval].n_type &= ~N_TYPE;
                as.stab[cval].n_type |= SEGMTYPE(as.segm);
                continue;
            } else if (clex == '=') {
                // "name = expr" - define a constant name.
                as.stab[cval].n_value = parse_expr(&csegm);
                if (csegm == SEXT)
                    fatal("indirect equivalence");
                as.stab[cval].n_type &= N_EXT;
                as.stab[cval].n_type |= SEGMTYPE(csegm);
                break;
            }
            fatal("bad command");
        case LACMD:
            // An assembler directive.  Most either switch the active segment or
            // emit data into it.
            switch (cval) {
            // Flush the current segment's pending half-word before switching:
            // emit_halfword() buffers the odd half-word in a single static pair
            // shared across segments, so a mid-word switch would otherwise let the
            // new segment's first half-word clobber (and drop) it.  align_segment
            // is a no-op when the segment is already whole-word aligned.
            case TEXT:
                align_segment(as.segm);
                as.segm = STEXT;
                break;
            case CONST:
                align_segment(as.segm);
                as.segm = SCONST;
                break;
            case DATA:
                align_segment(as.segm);
                as.segm = SDATA;
                break;
            case STRNG:
                align_segment(as.segm);
                as.segm = SSTRNG;
                break;
            case BSS:
                align_segment(as.segm);
                as.segm = SBSS;
                break;
            case ORG:
                // ".org addr" - lay the segment out by absolute address: put the
                // next word on address addr.  Only the const segment has a base
                // known here -- the linker loads it at CBASE, so addr sits at
                // offset addr-CBASE.  Every other segment's base depends on the
                // rest of the link, so an origin there would name an address this
                // assembler can neither compute nor check.
                if (as.segm != SCONST)
                    fatal(".org outside the const segment");
                align_segment(as.segm);
                addr = parse_expr(&csegm);
                if (csegm != SABS || addr < CBASE || addr > CONSTTOP)
                    fatal("bad .org address");
                set_location(addr - CBASE);
                break;
            case HALF:
                // ".half e, e, ..." - emit each value as one 24-bit half-word.
                for (;;) {
                    parse_expr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(as.extref);
                    emit_halfword(LOHALF(as.intval), addr);
                    if ((clex = next_token(&cval)) != ',') {
                        unget_token(clex, cval);
                        break;
                    }
                }
                break;
            case WORD:
                // ".word e, e, ..." - emit each value as one full 48-bit word
                // (low half carries the relocation, high half is absolute).
                // The halves go through emit_halfword() so that a word landing
                // in the const segment is offered to the dedup table.
                align_segment(as.segm);
                for (;;) {
                    parse_expr(&cval);
                    addr = SEGMREL(cval);
                    if (cval == SEXT)
                        addr |= RPUTIX(as.extref);
                    emit_halfword(HIHALF(as.intval), (long)RABS);
                    emit_halfword(LOHALF(as.intval), addr);
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
            case EQU:
                // ".equ name, expr" - define a constant name (directive form of
                // "name = expr").
                if (next_token(&cval) != LNAME)
                    fatal("bad parameter .equ");
                if (next_token(&tval) != ',')
                    fatal("bad parameter .equ");
                as.stab[cval].n_value = parse_expr(&csegm);
                if (csegm == SEXT)
                    fatal("indirect equivalence");
                as.stab[cval].n_type &= N_EXT;
                as.stab[cval].n_type |= SEGMTYPE(csegm);
                break;
            case COMM:
                // ".comm name, len" - the directive form of a common block.
                if (next_token(&cval) != LNAME)
                    fatal("bad parameter .comm");
                if (as.stab[cval].n_type != N_UNDF && as.stab[cval].n_type != (N_EXT | N_COMM))
                    fatal("name already defined");
                as.stab[cval].n_type = N_EXT | N_COMM;
                if ((clex = next_token(&tval)) == ',') {
                    parse_expr(&tval);
                    if (tval != SABS)
                        fatal("bad length .comm");
                } else {
                    unget_token(clex, cval);
                    as.intval = 1;
                }
                as.stab[cval].n_value = LOHALF(as.intval);
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
