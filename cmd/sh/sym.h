/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#ifndef SH_SYM_H
#define SH_SYM_H
/*
 *	UNIX shell
 */

/*
 * The number the parser knows each reserved word by.  msg.c's `reserved' table pairs
 * them with the words themselves.  The values are not arbitrary: SYMFLG (0400) is set in
 * all of them, so that one test tells a reserved word from an ordinary character, and
 * the overlapping low bits let chksym() accept several at once by OR-ing them together
 * -- as in "either `else' or `elif' or `fi' may close a then-part".
 */
#define DOSYM  0405
#define FISYM  0420
#define EFSYM  0422
#define ELSYM  0421
#define INSYM  0412
#define BRSYM  0406
#define KTSYM  0450
#define THSYM  0444
#define ODSYM  0441
#define ESSYM  0442
#define IFSYM  0436
#define FORSYM 0435
#define WHSYM  0433
#define UNSYM  0427
#define CASYM  0417

/* A doubled operator -- && || >> << -- is SYMREP OR-ed into the character. */
#define SYMREP  04000
#define ECSYM   (SYMREP | ';')
#define ANDFSYM (SYMREP | '&')
#define ORFSYM  (SYMREP | '|')
#define APPSYM  (SYMREP | '>')
#define DOCSYM  (SYMREP | '<')
#define EOFSYM  02000 /* end of input */
#define SYMFLG  0400  /* set in every reserved word's number; see above */

/* The `flg' argument to cmd() */
#define NLFLG 1 /* a newline may end this command */
#define MTFLG 2 /* an empty command is allowed here */

/*
 * OR-ed into the lexer's one-character pushback, so that a pushed-back NUL is still
 * distinguishable from "nothing pushed back".
 */
#define MARK 0100000

/* The characters the lexer treats specially, named rather than written as literals. */
#define DQUOTE  '"'
#define SQUOTE  '`'
#define LITERAL '\''
#define DOLLAR  '$'
#define ESCAPE  '\\'
#define BRACE   '{'

#endif // SH_SYM_H
