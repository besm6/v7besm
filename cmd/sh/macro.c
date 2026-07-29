/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Parameter and command substitution -- the $ and the backquote.
//
#include <unistd.h>

#include "defs.h"
#include "sym.h"

// The bit to OR into every character being copied: QUOTE while inside double quotes,
// 0 outside.  Marking each character as it goes past is how the shell remembers, later,
// which parts of a word were quoted and so must not be split or matched as a wildcard.
//
// IT MUST BE AN INT.  v7 held it in a CHAR because the mark was bit 0200 of the character
// and fitted; the mark is far above the byte now (defs.h), and a CHAR here would make
// `quote ^= QUOTE' a no-op and take out every double quote in the shell in silence.
static INT quote;

// Whether any double quote was seen at all.  A word written "" expands to an empty
// argument, which must survive; a word that was simply empty does not.
static CHAR quoted;

//
// Whether what is being built is a WORD or a BUFFER, which decides whether the characters
// pushed below are encoded (defs.h) or laid down as they stand.
//
// macro() is building a word on the expression stack, so the stored form applies and
// trim() will decode it later.  subst() is filling a buffer that flush() hands straight to
// write(), so nothing may go into it that the command reading it did not ask for.  The two
// share every routine in this file, which is why the difference is a flag and not a
// parameter.
//
static BOOL encoding;

// Defined below.
static INT getch(INT endch);
static void comsubst(void);
static void flush(INT ot);
static void skipto(INT endch);

//
// Push one character of expansion output -- see `encoding' above.
//
static void pushout(INT c)
{
    if (encoding) {
        pushq(c);
    } else {
        chkstak(staktop);
        pushstak(c & LOBYTE);
    }
}

//
// v7 declared this LOCAL STRING and never returned anything from it; both callers
// ignore the result, so it says void.
//
static void copyto(INT endch)
{
    INT c;

    while ((c = getch(endch)) != endch && c) {
        pushout(c | quote);
    }
    zerostak();
    if (c != endch)
        error(badsub);
}

//
// Skip characters up to }
//
//
// Throw away characters up to endch, honouring nested quotes and ${...}.
//
// Used for the half of ${name-word} that is NOT wanted: in `${x-y}' with x set, y still
// has to be scanned past correctly, but nothing in it should be evaluated.
//
// A quoted character cannot close anything: readc() hands the mark back with it, so
// `${x-a\}b}' skips past the escaped brace the way it should.
static void skipto(INT endch)
{
    INT c;

    while ((c = readc()) && c != endch) {
        switch (c) {
        case SQUOTE:
            skipto(SQUOTE);
            break;

        case DQUOTE:
            skipto(DQUOTE);
            break;

        case DOLLAR:
            if (readc() == BRACE)
                skipto('}');
            break;
        }
    }
    if (c != endch)
        error(badsub);
}

//
// Read the next character of an expansion, doing the $ substitutions as it goes.
//
// This is where ${...} is implemented.  When it meets a `$' it works out what is being
// named -- a variable, a positional parameter, or one of $$ $! $# $? $- -- and then what
// is being asked for:
//
//	${x}		the value
//	${x-word}	the value, or `word' if x is unset
//	${x=word}	likewise, and assign `word' to x as well
//	${x+word}	`word', but only if x IS set
//	${x?word}	the value, or fail with `word' as the message
//
// The value's characters are pushed on the expression stack from inside here, so the
// caller only ever sees ordinary text come back.  A backquote hands off to comsubst()
// and a double quote flips the `quote' bit above.
//
static INT getch(INT endch)
{
    INT d;

retry:
    d = readc();
    if (!subchar(d))
        return d;

    if (d == DOLLAR) {
        INT c;
        if (c = readc(), dolchar(c)) {
            NAMPTR n = (NAMPTR)NIL;
            INT dolg = 0;
            BOOL bra;
            STRING argp, v;
            CHAR idb[2];
            STRING id = idb;

            //
            // AN OFFSET, IN AN INT -- see stak.h.  v7 kept relstak()'s result in the
            // same STRING variable it later used as a real address, which works only
            // where an offset and a pointer are the same bits.  Here they are not, so
            // the offset has a variable of its own and `argp' stays a pointer.
            //
            INT argrel;

            if ((bra = (c == BRACE)) != 0)
                c = readc();
            if (letter(c)) {
                argrel = relstak();
                while (alphanum(c)) {
                    chkstak(staktop);
                    pushstak(c);
                    c = readc();
                }
                zerostak();
                n = lookup(absstak(argrel));
                setstak(argrel);
                v     = n->namval;
                id    = n->namid;
                peekc = c | MARK;
            } else if (digchar(c)) {
                *id    = c;
                idb[1] = 0;
                if (astchar(c)) {
                    dolg = 1;
                    c    = '1';
                }
                c -= '0';
                // v7 spelled the last arm (STRING)(dolg=0), casting an integer to a
                // char *; on this machine that fabricates a fat pointer out of a
                // number.  Written out instead.
                if (c == 0) {
                    v = cmdadr;
                } else if (c <= dolc) {
                    v = dolv[c];
                } else {
                    dolg = 0;
                    v    = 0;
                }
            } else if (c == '$') {
                v = pidadr;
            } else if (c == '!') {
                v = pcsadr;
            } else if (c == '#') {
                v = dolladr;
            } else if (c == '?') {
                v = exitadr;
            } else if (c == '-') {
                v = flagadr;
            } else if (bra) {
                error(badsub);
            } else {
                goto retry;
            }

            c = readc();
            if (!defchar(c) && bra)
                error(badsub);

            argp = 0;
            if (bra) {
                if (c != '}') {
                    argrel = relstak();
                    if (((v == 0) ^ setchar(c)) != 0)
                        copyto('}');
                    else
                        skipto('}');
                    argp = absstak(argrel);
                }
            } else {
                peekc = c | MARK;
                c     = 0;
            }

            if (v) {
                if (c != '+') {
                    for (;;) {
                        //
                        // A VALUE COMES OUT OF THE NAME TREE RAW -- see defs.h's third
                        // invariant -- so every byte of it is encoded on the way onto the
                        // stack.  Without that a 0377 in somebody's data would be read
                        // back by trim() as a quoting mark and eat the byte after it.
                        //
                        while ((c = *v++) != 0)
                            pushout(c | quote);
                        if (dolg == 0 || (++dolg > dolc)) {
                            break;
                        } else {
                            v = dolv[dolg];
                            pushout(SP | (*id == '*' ? quote : 0));
                        }
                    }
                }
            } else if (argp) {
                //
                // argp is the ${x?word} / ${x=word} tail, which copyto() has just left on
                // the stack in the STORED form.  It is about to be printed, or to become a
                // variable's value, and both want plain text.  (Under subst() copyto()
                // pushed plain text already, so there is nothing to take off.)
                //
                // v7 ASSIGNED IT MARKED, which is why this is a deliberate divergence and
                // not a bug fix: there, ${x=a\ b} put a 0200-marked space in x and a later
                // $x came out as one word.  The name tree holds raw values here.
                //
                if (encoding)
                    trim(argp);
                if (c == '?')
                    failed(id, *argp ? argp : badparam);
                else if (c == '=') {
                    if (n)
                        assign(n, argp);
                    else
                        error(badsub);
                }
            } else if (flags & setflg) {
                failed(id, badparam);
            }
            goto retry;
        } else {
            peekc = c | MARK;
        }
    } else if (d == endch) {
        return d;
    } else if (d == SQUOTE) {
        comsubst();
        goto retry;
    } else if (d == DQUOTE) {
        quoted++;
        quote ^= QUOTE;
        goto retry;
    }
    return d;
}

//
// Strip "" and do $ substitution.  Leaves the result on top of the stack.
//
//
// Expand $ and "" in a string, leaving the result on the expression stack.
//
// The trick is that the string is PUSHED AS AN INPUT and read back through the ordinary
// lexer machinery, so that substitution needs no scanner of its own -- and so that a
// value which itself contains a `$' is not expanded a second time, since the reading
// stops when the string does.
//
STRING macro(STRING as)
{
    BOOL savqu  = quoted;
    BOOL savenc = encoding;
    INT savq    = quote;
    SHFILEHDR fb;

    push((SHFILE)&fb);
    estabf(as);
    //
    // THE WORD IS IN THE STORED FORM and is about to be read back through the lexer, so
    // readc() has to decode it; and what comes out is a word again, so the pushes below
    // have to encode.  These are the two halves of defs.h's second invariant, and they are
    // separate flags because subst() wants the first without the second.
    //
    standin->fencd = TRUE;
    encoding       = TRUE;
    usestak();
    quote  = 0;
    quoted = 0;
    copyto(0);
    pop();
    if (quoted && (stakbot == staktop)) {
        // "" -- an empty word that must survive.  A lone QESC before the terminator is
        // what v7 spelled as a lone 0200, and every decoder reads it as `nothing'.
        chkstak(staktop);
        pushstak(QESC);
    }
    quote    = savq;
    quoted   = savqu;
    encoding = savenc;
    return fixstak();
}

//
// Command substitution.
//
//
// Command substitution: `command` .
//
// The text between the backquotes is collected, parsed as a shell command, and run in a
// child with its output going down a pipe this shell reads.  What comes back becomes the
// value of the expansion, with trailing newlines removed -- which is why ``x=`pwd`''
// does not put a newline in x.
//
static void comsubst(void)
{
    SHFILEBLK cb;
    INT d, lastnonnl;
    STKPTR savptr = fixstak();

    usestak();
    //
    // The body is collected out of the word being expanded, which is encoded, so readc()
    // hands back decoded characters and a quoted backquote does not end the collection.
    // It goes back down in the stored form because trim() below is what turns it into the
    // command text -- pushq() and not pushout(), because that is true under subst() too.
    //
    while ((d = readc()) != SQUOTE && d)
        pushq(d);

    {
        STRING argc;
        trim(argc = fixstak());
        push((SHFILE)&cb);
        estabf(argc);
    }
    {
        TREPTR t = makefork(FPOU, cmd(EOFSYM, MTFLG | NLFLG));
        INT pv[2];

        // this is done like this so that the pipe is open only when needed
        chkpipe(pv);
        initf(pv[INPIPE]);
        execute(t, 0, 0, pv);
        close(pv[OTPIPE]);
    }
    tdystak(savptr);
    staktop = movstr(savptr, stakbot);
    //
    // THE TRAILING NEWLINES ARE TRIMMED FORWARD, not backward.  v7 walked back down the
    // stack popping bytes that looked like a newline, and this encoding cannot be read
    // backwards at all: inside double quotes every output byte is laid down as `QESC d',
    // so the byte in front of a newline is a mark, and a byte 0377 in the output is laid
    // down as `QESC QESC', so the byte in front of a mark may be a payload.  Remembering
    // where the last non-newline ended costs one integer and no scan.
    //
    // It also fixes something v7 got wrong by accident: its loop stopped at `stakbot', so
    // the text ALREADY on the stack in front of the backquote could be eaten too if it
    // happened to end in a newline.  The floor here is where that text ends.
    //
    lastnonnl = relstak();
    while ((d = readc()) != 0) {
        pushout(d | quote);
        if ((d & LOBYTE) != NL)
            lastnonnl = relstak();
    }
    await(0);
    setstak(lastnonnl);
    pop();
}

//
// How much subst() accumulates before it writes.
//
// ONE DISK BLOCK, which is what v7 meant by it too -- it wrote 512, and BSIZE on the
// PDP-11 was 512 bytes.  A block here is 3072 bytes (BSIZEW 512 words times NBPW 6), so
// carrying the literal across would have been a sixth of a block and, worse, 85 1/3
// WORDS: not a whole number of them, so every flush would hand the kernel a byte count
// that is not a word multiple and take the slow path through iomove().  <stdio.h> sets
// BUFSIZ from BSIZE for the same reason and says so.
//
#define CPYSIZ BSIZE

//
// Copy descriptor `in' to descriptor `ot', expanding $ on the way.
//
// This is an unquoted here-document: its text is read out of the temp file, run through
// getch() so that $ and backquotes take effect, and written to the pipe or file the
// command will read.  The output is batched CPYSIZ characters at a time rather than
// written a character at a time.
//
void subst(INT in, INT ot)
{
    INT c;
    BOOL savenc = encoding;
    SHFILEBLK fb;
    INT count = CPYSIZ;

    push((SHFILE)&fb);
    initf(in);
    //
    // THE TEMP FILE IS ENCODED and what comes out of here is not.  copy() wrote it in the
    // stored form (io.c) so that a backslash in the body still means something by the time
    // it is read back; this is where the marks come off, and the command on the far end of
    // `ot' gets the bytes the user wrote.  v7 did the same job with a `& 0177' on the way
    // past, which is what cost an unquoted here-document its eighth bit.
    //
    standin->fencd = TRUE;
    encoding       = FALSE;
    // DQUOTE used to stop it from quoting
    while ((c = (getch(DQUOTE) & LOBYTE)) != 0) {
        chkstak(staktop);
        pushstak(c);
        if (--count == 0) {
            flush(ot);
            count = CPYSIZ;
        }
    }
    flush(ot);
    encoding = savenc;
    pop();
}

//
// Write out what subst() has accumulated on the stack, and empty it again.
//
static void flush(INT ot)
{
    write(ot, stakbot, staktop - stakbot);
    if (flags & execpr)
        write(output, stakbot, staktop - stakbot);
    staktop = stakbot;
}
