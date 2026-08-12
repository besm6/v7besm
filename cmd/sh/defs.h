/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The UNIX shell -- S. R. Bourne, Bell Telephone Laboratories.
//
// WHAT CHANGED, AND WHY.  Two things, and they are worth separating.
//
// C11.  b6parse has no implicit int, no K&R parameter lists and no untyped `register
// i;', so every function here has a prototype and an explicit return type.  v7's
// ALGOL-68 macro dialect (mac.h: IF/THEN/FI, LOOP/POOL, REP/PER/DONE, SWITCH/IN/ENDSW)
// is gone with it -- not because C11 rejects it, it preprocesses fine, but because the
// pointer fixes below have to be read and reviewed, and they cannot be while the
// control flow is spelled in macros clang-format cannot format.
//
// And this header no longer DEFINES the shell's globals.  v7 declared them here, in a
// header every file includes, as tentative definitions -- `INT flags;' -- and let the
// linker merge the twenty copies into one common block.  C11 has no such thing.  They
// are extern here and defined once in glob.c.
//
// THE MACHINE.  A BESM-6 word is 48 bits and the machine is word-addressed:
// sizeof(int) == 6 char-units == one word, `char' is unsigned, `char *' and `void *'
// are FAT pointers (a bit-48 marker, a 3-bit byte offset, a 15-bit word address) and
// every other pointer is a bare word address in bits 15-1.  See
// doc/Besm6_Data_Representation.md sections 7-8.  Three consequences run through this
// program, because the v7 shell is written for a byte-addressed machine on which an
// `int' and a `char *' are the same thing:
//
//   - A FLAG PACKED INTO A POINTER GOES IN BIT 16, NEVER IN BIT 0.  Bit 0 of a word
//     address names the neighbouring WORD.  blok.c's BUSY and service.c's ARGMK both
//     moved; lib/libc/gen/malloc.c is v7's same allocator with the same fix and carries
//     the full reasoning.  v7's Lcheat/Rcheat puns, which existed only to do this, are
//     gone from mode.h.
//   - ROUNDING TO A WORD IS NOT A BIT MASK.  BYTESPERWORD is 6, and `& ~(6-1)' is not a
//     rounding operation at all -- it rounds 7 to 8.  v7's round() is replaced by the
//     two macros below, one for byte counts and one for pointers.
//   - EVERY CAST FROM char * TO A NODE POINTER FLOORS.  It does not round: it drops the
//     byte offset and keeps the word.  So the whole parse tree and argument list, which
//     the shell builds by casting stack addresses to ARGPTR/COMPTR/STRING *, depends on
//     an invariant that has to hold and be stated: STAKBOT, STAKTOP AFTER endstak() OR
//     getstak(), AND EVERY shalloc() RESULT SIT AT BYTE #0 OF A WORD.  wordup() below
//     and sizeup() are what keep it.
//
#ifndef SH_DEFS_H
#define SH_DEFS_H

#include <setjmp.h>
#include <sys/param.h>
#include <sys/types.h>

// The exit statuses the shell gives itself when something goes wrong.
#define ERROR   1    // any ordinary failure -- cannot open, not found, ...
#define SYNBAD  2    // a syntax error in the input
#define SIGFAIL 3    // a signal arrived and there was no trap for it
#define SIGFLG  0200 // added to a signal number to make the $? of a child it killed

//
// A parse-tree node's type word holds the KIND in its low four bits and these flags
// above them, so that one word says both what the node is and how it is to be run.
//
#define FPRS   020   // print the child's pid, as an interactive shell does for `&'
#define FINT   040   // ignore interrupts, and read /dev/null: a `&' command
#define FAMP   0100  // started with `&': do not wait for it
#define FPIN   0400  // its input is a pipe
#define FPOU   01000 // its output is a pipe
#define FPCL   02000 // close the pipe in the parent once the child has it
#define FCMD   04000 // a command rather than a control structure
#define COMMSK 017   // the low bits: which of the T* kinds below this node is

// The kinds of parse-tree node.  execute() switches on these; mode.h has the struct for
// each one.
#define TCOM  0  // a simple command: a name and its arguments
#define TPAR  1  // ( ... )
#define TFIL  2  // a | b
#define TLST  3  // a; b
#define TIF   4  // if ... then ... else ... fi
#define TWH   5  // while ... do ... done
#define TUN   6  // until ... do ... done
#define TSW   7  // case ... in ... esac
#define TAND  8  // a && b
#define TORF  9  // a || b
#define TFORK 10 // run the child node in a separate process
#define TFOR  11 // for ... do ... done

// The built-in commands, numbered.  syslook() turns a command name into one of these and
// execute()'s inner switch runs it here rather than forking.
#define SYSSET    1
#define SYSCD     2
#define SYSEXEC   3
#define SYSLOGIN  4
#define SYSTRAP   5
#define SYSEXIT   6
#define SYSSHFT   7
#define SYSWAIT   8
#define SYSCONT   9
#define SYSBREAK  10
#define SYSEVAL   11
#define SYSDOT    12
#define SYSRDONLY 13
#define SYSTIMES  14
#define SYSXPORT  15
#define SYSNULL   16
#define SYSREAD   17
#define SYSTST    18
#define SYSUMASK  19

// Where the shell moves its OWN input and output at startup, well out of the range a
// user's redirection is likely to name, so that a command cannot capture the prompt or
// read the script out from under the shell.
#define INIO 10
#define OTIO 11

//
// A redirection's `iofile' word: which descriptor in the low bits, and what kind of
// redirection above them.
//
#define USERIO 10  // the highest descriptor a user may name in a redirection
#define IOUFD  15  // mask for the descriptor number itself
#define IODOC  16  // a here-document: <<WORD
#define IOPUT  32  // output: >
#define IOAPP  64  // append rather than truncate: >>
#define IOMOV  128 // move a descriptor: >&2, or close it: >&-
#define IORDW  256 // open for both reading and writing: <>

// A pipe() fills in two descriptors; these name them.
#define INPIPE 0 // the end to read from
#define OTPIPE 1 // the end to write to

// What marks the end of an argument vector -- a null pointer, as execve wants it.
#define ENDARGS 0

//
// The dialect mac.h used to spell.  Only the value macros survive; the control-flow ones
// are written out at their call sites.
//
#define TRUE   (-1)
#define FALSE  0
#define LOBYTE 0377 // mask for one byte

//
// ======== HOW A QUOTED CHARACTER IS MARKED ========
//
// v7 put the mark in BIT 0200 OF THE CHARACTER ITSELF and took it off again with a
// `& 0177'.  That costs the eighth bit of every byte the shell handles, and this machine
// needs it: the console is a byte pipe and its text is UTF-8 (kernel/dev/sc.c), so `echo
// привет' reached /bin/echo with every second byte bent into an ASCII letter.
//
// The mark is a BYTE OF ITS OWN here, and there are two representations of it.  Keeping
// them apart is the whole of the design.
//
// IN FLIGHT -- a character held in an INT: a register, a function's return value, peekc.
// The mark is the bit QUOTE below.  Its value is not arbitrary: wdval and peekc share one
// integer space with SYMFLG, EOFSYM, SYMREP and MARK (sym.h), and QUOTE is placed clear of
// all four so that no argument about which branches are reachable has to be made.  An INT
// is a 48-bit word, so the headroom is free.
//
// IN STORAGE -- the expression stack, the argnod words built on it, and the here-document
// temp file: the mark is a PREFIX BYTE, QESC.
//
//	an ordinary byte c		c
//	a quoted character c		QESC c
//	a literal 0377, quoted or not	QESC QESC
//	the empty quoted word ("")	a lone QESC just before the terminator
//
// So a bare QESC never appears except in that last case, which every decoder reads as
// `contributes nothing, end of string' -- it is what v7 spelled QUOTE|0 == 0200 and
// macro() still pushes for the same reason.  Quoted and unquoted 0377 share ONE encoding
// deliberately: the quotedness of 0377 cannot be observed (it is not a metacharacter, not
// a glob character and not in the default IFS), and collapsing them is what makes nosubst
// derivable -- `a QESC whose payload is not QESC was seen'.  See service.c's trim().
//
// Three invariants hold this together:
//
//   - THE STACK HOLDS ENCODED TEXT.  Anything entering it from outside -- a variable's
//     value, a command substitution's output, a directory entry's name, a here-document
//     line -- is encoded on the way in by pushq().  trim() is the one decoder on the way
//     out to argv.
//   - SCRIPT TEXT IS NEVER ENCODED.  readc() decodes only when the input it is reading was
//     flagged as an encoded one (mode.h's fencd), which is macro()'s re-read of a word it
//     pushed and subst()'s read of a here-document temp file.  And a parse-tree argval is
//     never mutated -- macro() re-runs over the same word on every loop iteration -- which
//     is what makes trim()'s in-place compaction safe.
//   - THE NAME TREE HOLDS RAW VALUES.  Not true in v7: `read' and ${x=word} both stored a
//     marked value.  Both are normalised here; see cmd/sh/README.md.
//
#define QUOTE 01000000 // in flight: this character is quoted
#define QESC  0377     // in storage: the next byte is a quoted character

// SHEOF, not EOF: <stdio.h> in this tree spells EOF as -1, and if it were ever reached
// the include order would decide whether word() can end a script at all.
#define SHEOF 0
#define NL    '\n'
#define SP    ' '
#define LQ    '`'
#define RQ    '\''
#define MINUS '-'
#define COLON ':'

// The comment character.  NOT v7 -- the Bourne shell got one in System III, and this port
// takes it: without it a `#' line is a command that is not found, and the stand-in is a
// `:' line whose words are still PARSED, so a backquote in one starts a command
// substitution that runs to end of file.  word() is where it is honoured.
//
// It works on the console too, which it did not while CERASE was `#' (<sys/tty.h>) and the
// line discipline ate a typed one -- and the character before it -- before the shell saw the
// line.  Erase is `^?' now, so a typed `#' arrives like any other character.
#define COMCHAR '#'

#include "mode.h"
#include "name.h"

#define attrib(n, f) (n->namflg |= f)                     // give a variable an attribute
#define closepipe(x) (close(x[INPIPE]), close(x[OTPIPE])) // close both ends of a pipe
#define eq(a, b)     (cf(a, b) == 0)                      // are two strings equal?
#define max(a, b)    ((a) > (b) ? (a) : (b))

//
// Round a byte count up to a whole number of words.  v7 wrote round(n,BYTESPERWORD) as
// a bit mask, which needs a power-of-two modulus; here the modulus is 6.
//
#define sizeup(n) ((((n) + BYTESPERWORD - 1) / BYTESPERWORD) * BYTESPERWORD)

//
// Round a char * up to the next word boundary -- the other half of v7's round(), and
// the one that keeps the invariant this header opens with.
//
// It works BECAUSE the cast floors.  char * -> int * drops the fat pointer's byte
// offset and keeps its word, so adding BYTESPERWORD-1 first and flooring afterwards is
// exactly a ceiling; casting back yields a fat pointer at byte #0 of that word.  No
// arithmetic on the pointer's integer value is involved, which is the point: there is
// none that would be correct.
//
#define wordup(p) ((STKPTR)(INT *)((p) + (BYTESPERWORD - 1)))

// stack
#define BLK(x) ((BLKPTR)(x))
#define BYT(x) ((BYTPTR)(x))
#define STK(x) ((STKPTR)(x))
#define ADR(x) ((char *)(x))

#include "stak.h"

//
// The linker's end-of-bss symbol (b6ld defines it, cmd/ld/ld.c), where the arena
// starts.  Declared as an array, so that its decay produces a genuine fat pointer at
// offset 0 rather than a cast -- lib/libc/sys/sbrk.c declares it the same way and for
// the same reason.  v7 wrote `address end[]', a tentative definition of a LINKER symbol
// in a header; left that way the shell would define an object of its own and blok.c's
// arena would start in the wrong place.
//
extern char end[];

// ======== the globals, defined in glob.c unless the comment names another file ========

// --- where the shell's own messages go, and the files it must clean up ---

// The descriptor the shell PRINTS ON.  Not 1: exfile() moves the original standard error
// to descriptor 11 at startup (OTIO) so that a command's redirections cannot capture the
// shell's own diagnostics or its prompt.
extern UFD output; // main.c

// Non-zero once a redirection has touched descriptor 0 for the command being built, so
// that a background command does not also get /dev/null attached to its input.
extern INT ioset;

// The chain of here-document temp files created so far.  rmtemp() unlinks them.
extern IOPTR iotemp;

// Here-documents whose text has been PROMISED (`cmd <<WORD') but not yet read.  The
// lexer collects them when it reaches the end of the line -- which is where the text
// actually starts.
extern IOPTR iopend;

// --- the positional parameters, and argument generation ---

// $# : how many positional parameters there are.
extern INT dolc;

// $1, $2, ... : the vector of them.  dolv[0] is $0, the name the shell was invoked as.
extern STRING *dolv;

// The `for' loops' saved parameter lists, kept alive while a loop is running so that a
// `set' inside the body cannot pull the list out from under it.
extern DOLPTR argfor;

// The chain of argument words built so far for the command being assembled.  scan()
// turns it into the argv the child gets.  ITS LOW-ORDER TAG BIT IS NOT BIT 0 -- see
// service.c's ARGMK.
extern ARGPTR gchain;

// --- fixed strings the shell prints (msg.c) ---

extern MSG atline;     // " at line " -- part of the syntax error report
extern MSG readmsg;    // "> " -- the default PS2, the continuation prompt
extern MSG colon;      // ": " -- the separator in "sh: something: message"
extern MSG minus;      // "-" -- tested against a redirection target, as in `2>&-'
extern MSG nullstr;    // "" -- the empty string, used wherever one is needed
extern MSG sptbnl;     // " \t\n" -- the default IFS, the word separators
extern MSG unexpected; // " unexpected" -- the tail of the syntax error report
extern MSG endoffile;  // "end of file" -- how the parser names the EOF symbol
extern MSG synmsg;     // "syntax error"

// --- the keyword tables, and what the lexer last read ---

// The reserved words: if, then, else, for, do, done, case ... Each maps to a symbol
// number that the parser switches on.
extern SYSNOD reserved[]; // msg.c

// The built-in commands: cd, set, export, trap, exit ... Each maps to a SYS* number that
// execute()'s inner switch runs directly instead of forking.
extern SYSNOD commands[]; // msg.c

// The symbol word() just read: 0 for an ordinary word, otherwise a reserved-word or
// operator code from sym.h.  The parser reads nothing else.
extern INT wdval;

// The digit in front of a redirection, as in `2>file' -- 2 here.  0 if there was none.
extern INT wdnum;

// The ordinary word word() just read, when wdval is 0.
extern ARGPTR wdarg;

// Non-zero if that word looks like an assignment (`name=value') rather than a command
// argument, so that `a=1 b=2 cmd' can set variables just for cmd.
extern INT wdset;

// Set by the parser before calling word() to say "a reserved word is legal HERE".
// `echo if' must not be parsed as an `if' statement, and this is what stops it.
extern BOOL reserv;

// --- prompting (msg.c) ---

extern MSG stdprompt; // "$ " -- PS1 for an ordinary user
extern MSG supprompt; // "# " -- PS1 for the super-user, so root looks different
extern MSG profile;   // ".profile" -- the login script a `-' shell reads first

// --- the shell variables that always exist (name.c) ---
//
// These seven are pre-built nodes of the name tree rather than ordinary entries, because
// the shell itself reads them and they must be there before any script runs.

extern NAMNOD fngnod;  // FILEMATCH
extern NAMNOD ifsnod;  // IFS -- the characters that separate words
extern NAMNOD homenod; // HOME -- where a bare `cd' goes
extern NAMNOD mailnod; // MAIL -- the file watched for "you have mail"
extern NAMNOD pathnod; // PATH -- the directories a command is looked for in
extern NAMNOD ps1nod;  // PS1 -- the main prompt
extern NAMNOD ps2nod;  // PS2 -- the continuation prompt

// --- the parameters that are not variables ---
//
// $-, $0, $?, $#, $! and $$ are not in the name tree: the shell keeps each as a plain
// string and rewrites it when the value changes.

extern MSG flagadr;    // $- : the letters of the options in force (args.c)
extern STRING cmdadr;  // $0 : the name the shell, or the script, was invoked as
extern STRING exitadr; // $? : the last command's exit status, in decimal
extern STRING dolladr; // $# : how many positional parameters, in decimal
extern STRING pcsadr;  // $! : the pid of the last `&' command, in decimal
extern STRING pidadr;  // $$ : this shell's own pid, in decimal

// ":/bin:/usr/bin" -- the PATH used when the variable is unset.  The leading empty entry
// means "the current directory first".
extern MSG defpath; // msg.c

// --- the names of those variables, as strings (msg.c) ---

extern MSG mailname; // "MAIL"
extern MSG homename; // "HOME"
extern MSG pathname; // "PATH"
extern MSG fngname;  // "FILEMATCH"
extern MSG ifsname;  // "IFS"
extern MSG ps1name;  // "PS1"
extern MSG ps2name;  // "PS2"

// --- reading input ---

// "/tmp/sh" followed by this shell's pid and a serial number: the name of the next
// here-document temp file.  settmp() writes the pid in, tmpfil() the serial.
extern CHAR tmpout[]; // main.c

// Where in tmpout[] the serial number goes -- just past the pid.
extern STRING tmpnam;

// Counts up so that each here-document in one shell gets a different temp file.
extern INT serial;

// Where the pid starts in tmpout[]: the length of "/tmp/sh".
#define TMPNAM 7

// THE INPUT STACK.  standin is the file the shell is reading commands from, and each
// one points at the one it interrupted (fstak), so that `.', `eval' and a here-document
// can read from somewhere else and then hand control back.  push() and pop() work it.
extern SHFILE standin; // main.c

// Shorthands for the two fields of the current input that the whole shell reads.
#define input (standin->fdes)
#define eof   (standin->feof)

// One character of pushback: the lexer often has to look at the next character to decide
// what the current one meant, and this is where it puts it back.  Bit MARK is set so
// that a pushed-back NUL is still distinguishable from "nothing pushed back".
extern INT peekc;

// The command string given with -c, as in `sh -c "echo hi"'.
extern STRING comdiv;

// Whether -c was given at all.  A separate flag because comdiv itself may legitimately
// be a null pointer; see main.c, where v7 instead decremented the null.
extern BOOL comdivset;

extern MSG devnull; // "/dev/null" -- what a `&' command gets for input (msg.c)

// Set by trim(): whether the string it just processed contained any quoted character.
// A here-document whose terminator was quoted (`<<\EOF') is copied literally.
//
// v7 computed this as the OR of every character against 0200, which on an eight-bit-clean
// machine goes true for any text at all with a high byte in it -- one Cyrillic letter in a
// here-document terminator and the body stopped being substituted.  It counts QESC pairs
// now, and a QESC QESC pair (a literal 0377) is not one of them.
extern BOOL nosubst;

// --- the option flags, one bit each ---
//
// Most come from the command line (`sh -x'); the shell sets the rest itself as it learns
// about its situation.  args.c's flagchar[]/flagval[] pair the letters with the bits.

#define noexec 01  // -n: parse but do not run.  Syntax check only.
#define intflg 02  // -i: interactive, whatever the input turns out to be
#define prompt 04  // print a prompt before each command (the shell sets this)
#define setflg 010 // -u: an unset variable is an error, not an empty string
#define errflg 020 // -e: exit as soon as any command fails
#define ttyflg 040 // input and output are both terminals (the shell sets this)
#define forked \
    0100            // this process is a CHILD; it may exit rather than return to the
                    // prompt (the shell sets this)
#define oneflg 0200 // -t: read and run one command, then exit
#define rshflg \
    0400 // restricted shell: no cd, no /-in-a-command-name, no output
         // redirection
#define waiting \
    01000             // sitting at the prompt with nothing to do, so the timeout alarm
                      // may log this shell out (the shell sets this)
#define stdflg 02000  // -s: read commands from standard input
#define execpr 04000  // -x: print each command before running it
#define readpr 010000 // -v: print each input line as it is read
#define keyflg \
    020000 // -k: `name=value' anywhere in a command is an assignment, not
           // just at the front

// All of the above, OR-ed together.
extern INT flags;

// --- the two long jumps ---
//
// A `longjmp' abandons whatever the shell was doing and resumes at the matching
// `setjmp', however deep the call stack had become.  The shell has two places worth
// resuming at.

// Back to the top of main(), used when a file turns out to be a SCRIPT rather than a
// binary: the shell must start over reading it, from wherever exec() failed.
extern jmp_buf subshell;

// Back to the command loop, used by every fatal error: an interactive shell prints the
// message and goes back to the prompt rather than exiting.
extern jmp_buf errshell;

// --- growing the arena ---

#include "brkincr.h"

// How much memory to ask the system for at a time, in char-units.  It grows as the shell
// turns out to need more, up to BRKMAX, so that a big script does not make hundreds of
// small requests.
extern POS brkincr; // blok.c

// The range of signal numbers `trap' will accept.  0 is not a signal: it is the
// pseudo-trap that runs when the shell exits.
#define MINTRAP 0
#define MAXTRAP 17

// The four signals the shell itself cares about.
#define INTR  2  // SIGINT -- the interrupt key
#define QUIT  3  // SIGQUIT -- the quit key
#define MEMF  11 // SIGSEGV -- out of memory; fault() grows the arena and carries on
#define ALARM 14 // SIGALRM -- the idle timeout at the prompt
#define KILL  15 // SIGTERM

// The three bits kept per signal in trapflg[] below.
#define TRAPSET 2 // it fired, and the user has a `trap' command for it to run
#define SIGSET  4 // it fired, and there is no trap: the shell should give up
#define SIGMOD \
    8 // the shell CHANGED this signal's disposition, so a child must have it
      // put back before exec

// Non-zero when some signal has fired and has not been dealt with yet.  The shell tests
// this at safe points rather than doing the work inside the handler.
extern BOOL trapnote;

// The command string the user gave to `trap', one per signal.  Null if none.
extern STRING trapcom[]; // fault.c

// TRAPSET/SIGSET/SIGMOD for each signal.
extern BOOL trapflg[]; // fault.c

// Room for the decimal form of an INT: thirteen digits of a 41-bit signed value, a sign
// and a NUL.  v7's numbuf was six bytes, which was all a 16-bit int ever needed.
#define NUMBUF 16

// Where itos() leaves the decimal form of a number, and prn() prints it from.  One
// shared buffer, so a second conversion overwrites the first.
extern CHAR numbuf[]; // print.c

extern MSG export;   // "export" -- both the built-in's name and its listing prefix
extern MSG readonly; // "readonly" -- likewise

// One message per signal, for reporting how a child died: "Killed", "Quit", ... A null
// entry means "say nothing", which is why an interrupted command prints no fuss.
extern STRING sysmsg[]; // msg.c

// --- how the tree walker keeps its place ---

// The exit status of the last command; what $? is made from.
extern INT exitval;

// Set by `break' and `continue' to say "stop walking the tree and unwind": positive to
// break out, negative to go round again.  Every loop in execute() tests it.
extern BOOL execbrk;

// How many loops are running, so that `break' outside one can be ignored.
extern INT loopcnt;

// How many more levels `break 2' still has to break out of.
extern INT breakcnt;

// --- the error messages (msg.c) ---

extern MSG mailmsg;    // "you have mail\n"
extern MSG coredump;   // " - core dumped"
extern MSG badopt;     // "bad option(s)"
extern MSG badparam;   // "parameter not set" -- from ${x?} or -u
extern MSG badsub;     // "bad substitution" -- a malformed ${...}
extern MSG nospace;    // "no space" -- out of memory
extern MSG toodeep;    // "too deep" -- out of machine stack (deepchk)
extern MSG notfound;   // "not found" -- no such command on PATH
extern MSG badtrap;    // "bad trap" -- a signal number out of range
extern MSG baddir;     // "bad directory" -- cd failed
extern MSG badshift;   // "cannot shift" -- shift with no parameters left
extern MSG illegal;    // "illegal io" -- a built-in cannot be redirected
extern MSG restricted; // "restricted" -- barred by the -r shell
extern MSG execpmsg;   // "+ " -- the prefix -x puts on each command it echoes
extern MSG notid;      // "is not an identifier" -- a bad variable name
extern MSG wtfailed;   // "is read only" -- assignment to a readonly variable
extern MSG badcreate;  // "cannot create" -- a `>' failed
extern MSG piperr;     // "cannot make pipe"
extern MSG badopen;    // "cannot open" -- a `<' failed
extern MSG badnum;     // "bad number" -- a numeric argument was not numeric
extern MSG arglist;    // "arg list too long"
extern MSG txtbsy;     // "text busy" -- the file is being written
extern MSG toobig;     // "too big" -- the program will not fit in memory
extern MSG badexec;    // "cannot execute" -- found, but not runnable
extern MSG badfile;    // "bad file number" -- a redirection named a silly descriptor

// ======== the functions ========

// --- args.c: the command line and the positional parameters ---

// Read the shell's own options off the command line and set `flags'; return how many
// arguments are left for the script.
INT options(INT argc, STRING *argv);

// Replace $1, $2, ... with a new list, copying it into the arena.
void setargs(STRING argi[]);

// Drop one reference to a saved parameter list, freeing it when the last goes.
DOLPTR freeargs(DOLPTR blk);

// Abandon everything the shell was in the middle of: `for' lists and stacked inputs.
// Called on the way out of an error.
void clearup(void);

// Take one more reference to the current parameter list, so that a `for' loop can walk
// it while the body is free to call `set'.
DOLPTR useargs(void);

//
// --- blok.c: the shell's own memory allocator ---
//
// v7 named these alloc()/free() and then `#define alloc malloc' in this header, so the
// shell's own arena DEFINED malloc and free.  That works only as long as nothing drags
// libc's malloc.o into the link, and half of stdio references it.  The shell keeps its
// own allocator under its own names instead, and libc's is free to coexist unreferenced.
//

// Hand out `nbytes' char-units of arena, growing it if need be.  Never returns null: it
// grows or it reports "no space" and gives up.
ADDRESS shalloc(POS nbytes);

// Grow the arena by at least `reqd' char-units and move the expression stack up on top
// of the new space.
void addblok(POS reqd);

// Give a block back.  Coalescing happens later, during a search.
void shfree(BLKPTR ap);

// --- builtin.c ---

// The hook for locally added built-in commands.  Returns 0 -- "not mine, go and fork".
INT builtin(INT argn, STRING *com);

// --- cmd.c: the parser ---

// Wrap a piece of the tree in a node that says "run this in a child process".
TREPTR makefork(INT flgs, TREPTR i);

// Parse one complete command and return the tree for it.  `sym' is the symbol that must
// close it (`fi' for an if, 0 at the top level); `flg' says whether a newline ends it and
// whether emptiness is allowed.
TREPTR cmd(INT sym, INT flg);

// --- error.c: giving up ---

// Rewrite $? from `exitval'.  Called after every command.
void exitset(void);

// Give up if a signal has fired and there is no trap to handle it.  Called at every
// point where stopping is safe.
void sigchk(void);

// Give up if the recursion over the parse tree has used the MACHINE stack up.  Called
// from execute() and cmd(), the two functions that recurse with the script's nesting.
void deepchk(void);

// The bottom of the machine stack, which deepchk() measures against.
extern STKPTR stackfloor; // main.c

// Print "sh: s1: s2" and abandon the command.  DOES NOT RETURN.
_Noreturn void failed(STRING s1, STRING s2);

// Print "sh: s" and abandon the command.  DOES NOT RETURN.
_Noreturn void error(STRING s);

// Abandon the command with status `xno': back to the prompt if interactive, otherwise
// out of the shell altogether.  DOES NOT RETURN.
_Noreturn void exitsh(INT xno);

// Leave the shell for good: run the exit trap, remove the temp files, exit.  DOES NOT
// RETURN.
_Noreturn void done(void);

// Unlink the here-document temp files created above stack position `base'.
void rmtemp(STKPTR base);

// --- expand.c: filename generation ---

// Expand one word against the file system, adding every match to the argument chain, and
// return how many there were.  0 means "no wildcards, or nothing matched" and the word
// stands as written.
INT expand(STRING as, INT rflg);

// Does name `s' match pattern `p'?  The pattern language is *, ? and [...].
INT gmatch(STRING s, STRING p);

// Push one finished word onto the chain of arguments for this command.
void makearg(STRING args);

// --- fault.c: signals ---

// THE SIGNAL HANDLER.  Records that the signal fired -- or, for a memory fault, grows
// the arena on the spot.
void fault(INT sig);

// Set up the signals a shell always wants at startup.
void stdsigs(void);

// Ignore signal n; return whether it was ALREADY ignored, which tells the shell it was
// started that way and must leave it alone.
INT ignsig(INT n);

// Catch signal n with fault(), unless it was already being ignored.
void getsig(INT n);

// Put every signal back the way the parent had it.  A child does this before exec.
void oldsigs(void);

// Forget the trap on signal i and restore the default handling.
void clrsig(INT i);

// Run the trap commands for whatever fired since the last check.  Called between
// commands, never inside the handler.
void chktrap(void);

// --- io.c: descriptors, and the input stack ---

// Point the current input at descriptor `fd' and empty its buffer.
void initf(UFD fd);

// Point the current input at a STRING instead of a file -- how `eval' and $(...) read
// text that is already in memory.  Returns true if there was nothing to read.
INT estabf(STRING s);

// Start reading from `af', remembering where we were.
void push(SHFILE af);

// Go back to the input we came from; false if there is none.
INT pop(void);

// Make a pipe, or give up with "cannot make pipe".
void chkpipe(INT *pv);

// Open a file for reading, or give up with "cannot open".
INT chkopen(STRING idf);

// Move descriptor f1 onto f2 and close f1 -- what `2>&1' does.
void shrename(INT f1, INT f2); // ISO C owns the name `rename'

// Create a file for writing, or give up with "cannot create".
INT create(STRING s);

// Create the next here-document temp file and return its descriptor.
INT tmpfil(void);

// Read a here-document out of the input and into a temp file.
void copy(IOPTR ioparg);

// --- macro.c: $ substitution ---

// Expand $ and "" in a string, leaving the result on the expression stack.
STRING macro(STRING as);

// Copy descriptor `in' to descriptor `ot', expanding $ on the way: a here-document that
// was not quoted.
void subst(INT in, INT ot);

// --- main.c ---

// Print the continuation prompt, if this is an interactive shell and the line is not
// finished.
void chkpr(INT eor);

// Build the here-document temp file name out of this shell's pid.
void settmp(void);

// --- name.c: the variables ---

// Look a word up in a keyword table (`reserved' or `commands'); 0 if it is not there.
INT syslook(STRING w, SYSPTR syswds);

// Perform a list of `name=value' assignments.
void setlist(ARGPTR arg, INT xp);

// Perform one `name=value' assignment, giving the variable the attributes in xp.
void setname(STRING argi, INT xp);

// Replace the string at *a with a fresh copy of v, freeing the old one.
void replace(STRING *a, STRING v);

// Give a variable a value only if it has none yet.
void dfault(NAMPTR n, STRING v);

// Assign to a variable, refusing if it is readonly.
void assign(NAMPTR n, STRING v);

// The `read' built-in: read one line and split it across the named variables.
INT readvar(STRING *names);

// Set a string to the decimal form of a number -- how $? and $# are kept.
void assnum(STRING *p, INT i);

// Copy a string into the arena so that it outlives the expression stack.
STRING make(STRING v);

// Find a variable by name in the name tree, creating it if it is not there.
NAMPTR lookup(STRING nam);

// Call `fn' once for every variable, in alphabetical order.
void namscan(void (*fn)(NAMPTR));

// Print one variable as `name=value'.  What `set' with no arguments uses.
void printnam(NAMPTR n);

// Bring one variable's exported copy into step with its value, ready for an exec.
void exname(NAMPTR n);

// Print one variable's export/readonly attributes.  What `export' with no arguments uses.
void printflg(NAMPTR n);

// Read the environment this shell was started with into the name tree.
void readenv(void); // v7 called this getenv(); libc owns that name

// Build the environment vector to hand to a child, on the expression stack.
STRING *shenv(void); // ... and this one setenv()

// namscan() helpers: count the variables, then copy them out.
void countnam(NAMPTR n);
void pushnam(NAMPTR n);

// --- print.c: output and number conversion ---

void newline(void); // print one newline
void blank(void);   // print one space

// Print "sh: " -- the prefix on every diagnostic, so the user knows who is complaining.
void prp(void);

void prs(STRING as); // print a string
void prc(CHAR c);    // print one character

// Print a time in the h/m/s form the `times' built-in uses.
void prt(L_INT t);

// Print a number in decimal.
void prn(INT n);

// Convert a number to decimal in numbuf[].
void itos(INT n);

// Convert a decimal string to a number, giving up with "bad number" if it is not one.
INT stoi(STRING icp);

// --- service.c: running a command ---

// Perform a command's redirections: open the files and move the descriptors.
void initio(IOPTR iop);

// The list of directories to look for a command in -- PATH, or a fixed default.
STRING getpath(STRING s);

// Try to open `name' in each directory of `path' in turn; the descriptor, or -1.
INT pathopen(STRING path, STRING name);

// Join one directory of `path' to `name' on the expression stack; return what is left of
// the path.
STRING catpath(STRING path, STRING name);

// Run an external command: search PATH and exec it.  Only returns on failure -- and then
// only to report it.
void execa(STRING at[]);

// Forget every child this shell was waiting for.  A new child does this after forking.
void postclr(void);

// Remember one more child to wait for.
void post(INT pcsid);

// Wait for child `i' -- or for all of them, if -1 -- reporting any that died on a signal
// and setting $? from the result.
void await(INT i);

// Take the quoting marks off a string, now that quoting has done its work: decode the
// QESC pairs in place and record in `nosubst' whether there was a real one.
void trim(STRING at);

// macro() then trim(): the usual pair, for a word that is about to be used as a name.
STRING mactrim(STRING s);

// Turn the chain of argument words into the argv a child gets, sorting each group of
// filename matches.
STRING *scan(INT argn);

// Expand a command's arguments -- $ substitution, word splitting, wildcards -- and
// return how many words came out.
INT getarg(COMPTR ac);

// --- setbrk.c ---

// Move the program break by `incr' char-units and record where it reaches; 0 if the
// system refused.
INT setbrk(INT incr);

// --- stak.c: the expression stack ---
//
// Scratch space for words, arguments and parse-tree nodes, sitting on top of the arena.

// Reserve `asize' char-units and return the start of them.
STKPTR getstak(INT asize);

// Start building an item.  Returns where to write, having first made sure there is room.
STKPTR locstak(void);

// The current position, to be handed to tdystak() later to unwind back to.
STKPTR savstak(void);

// Finish the item that locstak() started, at `argp'; return its address.
STKPTR endstak(STRING argp);

// Throw the stack back to position x, returning to the arena anything the heap has since
// grown over.  This is how the shell forgets one command's working storage.
void tdystak(STKPTR x);

// Give memory back to the system if the stack is sitting on a lot of slack.
void stakchk(void);

// Copy a string onto the stack and finish the item in one go.
STKPTR cpystak(STKPTR x);

// --- string.c: the shell's own string routines ---
//
// Not libc's, and the differences matter: see the file.

// Copy a to b; return a pointer to the NUL that was written, so that copies can be
// chained end to end.
STRING movstr(STRING a, STRING b);

// Does character c occur anywhere in string s?  c is an INT and not a CHAR because the
// callers hand it one still carrying the in-flight QUOTE bit: a quoted space must NOT be
// found in IFS, and truncating it here would split the word.
INT any(INT c, STRING s);

// Read one character out of encoded text, advancing *p past however many bytes it took.
// The result carries QUOTE if it was marked; 0 means end of string, which includes the
// lone QESC that stands for an empty quoted word.
INT nextq(STRING *p);

// Compare two strings: negative, zero or positive, like strcmp.
INT cf(STRING s1, STRING s2);

// The length of a string INCLUDING its terminating NUL.
INT length(STRING as);

// --- word.c: the lexer ---

// Read the next word or operator from the input.  Leaves an ordinary word in `wdarg' and
// returns 0; otherwise returns the operator's symbol number.
INT word(void);

// Read the next character, handling backslash escapes.  `quote' is the quote character
// currently open, or 0.
INT nextc(INT quote);

// Read one raw character of input, refilling the buffer and popping the input stack as
// needed.  Everything the shell reads comes through here.
INT readc(void);

//
// --- xec.c: running the tree the parser built ---
//
// execute() is v7's one genuinely variadic function: it is written with four parameters
// and called with two, three or four, which K&R allowed and C11 does not.  It takes four
// now, and the pipe-less call sites pass NULL twice.
//

// Walk one node of the parse tree and do what it says, recursing into the children.
// This is the heart of the shell.  `execflg' says the caller will not need this process
// afterwards, so a command may exec in place rather than forking; pf1 and pf2 are the
// pipes to read from and write to, or NULL.  Returns the exit status.
INT execute(TREPTR argt, INT execflg, INT *pf1, INT *pf2);

//
// execexp() had the same shape in miniature: v7 declared its second parameter a file
// descriptor and then passed it a STRING * from the `eval' builtin, laundering a
// pointer through an int.  The two uses are separate parameters here.
//
// Run shell commands from somewhere other than the current input: from the string `s'
// (`eval', and a trap), or from the already-open descriptor `fd' (the `.' built-in).
// `args' is eval's argument vector, or NULL.
//
void execexp(STRING s, UFD fd, STRING *args);

#include "ctype.h"

#endif // SH_DEFS_H
