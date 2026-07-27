// The termcap library: /etc/termcap and the capabilities read out of it.
//
// V7 HAD NO SUCH HEADER.  termlib shipped as three .c files and nothing else, so every
// caller wrote its own `char *tgetstr();' beside the call -- which is how the v7 curses.h
// that used to stand in this directory declared nothing of what it used through _puts().
// The 4.3BSD one that replaced it includes this header instead.  This one is the
// repo's, and it exists because lib/libtermcap/termcap.c includes it: a prototype the
// definition is checked against is worth more here than v7 fidelity, since on this
// machine a missed `char *' return is a fat pointer truncated to a word address (see
// ../doc/Besm6_Data_Representation.md) rather than a value that merely looks wrong.
//
// NO PC AND NO ospeed, deliberately.  In 4.xBSD those two are termlib's, and tputs()
// emits PC characters proportional to the capability's delay and the line speed.  There
// is no serial line here -- the Consul-254 runs at model speed and SIMH hands a character
// over as fast as the driver offers it -- so lib/libtermcap/tputs.c parses the delay
// (it has to: the digits are part of the capability string) and emits no padding at all.
// Nothing therefore reads a line speed, and the names stay free for lib/libcurses, whose
// curses.c defines `char PC;' and whose cr_tty.c defines `short ospeed' -- both written
// there and read by nothing, exactly as 4.xBSD divided the work.
//
// The argument types are v7's `char *', not `const char *'.  tgetstr() writes through
// its second argument and tgoto()'s result is a static buffer, so the API is not
// const-clean at either end and saying so half way would only force casts on callers.

#ifndef _TERM_H
#define _TERM_H

// The database.  tgetent() fills bp -- which must hold TBUFSIZ (1024) characters -- from
// $TERMCAP or /etc/termcap, and returns 1 on a match, 0 if the terminal is not described,
// -1 if the database could not be opened.  The other four read the entry it left behind,
// so tgetent() must have succeeded first.
int tgetent(char *bp, char *name);

// A numeric capability (`co#80'), or -1 if the entry does not give it.
int tgetnum(char *id);

// A boolean capability (`am'), 1 if present, 0 if absent or cancelled with `@'.
int tgetflag(char *id);

// A string capability (`cl=\E[H\E[J'), decoded, copied into *area and *area advanced past
// it; 0 if the entry does not give it.  There is no bound on area -- the caller sizes it.
char *tgetstr(char *id, char **area);

// Cursor addressing: the `cm' capability with the row and column substituted.  The result
// is a static buffer, overwritten by the next call, and is the string "OOPS" if cm was 0
// or used a % form this tgoto does not implement.
char *tgoto(char *cm, int destcol, int destline);

// Write a capability out one character at a time through outc, stripping the leading
// padding delay.  affcnt is the number of lines the operation affects, which a `*' delay
// would scale by; nothing here pads, so it is accepted and unused.  Void, as 4.xBSD has
// it -- the int return is ncurses' OK/ERR and there is nothing here to report.
void tputs(char *cp, int affcnt, int (*outc)(int));

#endif // _TERM_H
