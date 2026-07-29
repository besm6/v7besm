/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#ifndef SH_CTYPE_H
#define SH_CTYPE_H
/*
 *	UNIX shell
 *
 *	S. R. Bourne
 *	Bell Telephone Laboratories
 *
 */

/*
 * The shell classifies a character by looking it up in one of two tables in ctype.c and
 * testing a bit, rather than by comparing it against a list.  These are the bits.
 *
 * Table 1 is what the LEXER needs to know.
 */
#define T_SUB 01
#define T_MET 02
#define T_SPC 04
#define T_DIP 010
#define T_EOF 020
#define T_EOR 040
#define T_QOT 0100
#define T_ESC 0200

/* Table 2 is what $ SUBSTITUTION and filename matching need to know. */
#define T_BRC 01
#define T_DEF 02
#define T_AST 04
#define T_DIG 010
#define T_FNG 020
#define T_SHN 040
#define T_IDC 0100
#define T_SET 0200

/*
 * The entry for one character, spelled by what the character is rather than by which
 * bits it has.  ctype.c's tables are written out in these.
 */
#define _TAB  (T_SPC)
#define _SPC  (T_SPC)
#define _UPC  (T_IDC)
#define _LPC  (T_IDC)
#define _DIG  (T_DIG)
#define _EOF  (T_EOF)
#define _EOR  (T_EOR)
#define _BAR  (T_DIP)
#define _HAT  (T_MET)
#define _BRA  (T_MET)
#define _KET  (T_MET)
#define _SQB  (T_FNG)
#define _AMP  (T_DIP)
#define _SEM  (T_DIP)
#define _LT   (T_DIP)
#define _GT   (T_DIP)
#define _LQU  (T_QOT | T_ESC)
#define _BSL  (T_ESC)
#define _DQU  (T_QOT)
#define _DOL1 (T_SUB | T_ESC)

#define _CBR  T_BRC
#define _CKT  T_DEF
#define _AST  (T_AST | T_FNG)
#define _EQ   (T_DEF)
#define _MIN  (T_DEF | T_SHN)
#define _PCS  (T_SHN)
#define _NUM  (T_SHN)
#define _DOL2 (T_SHN)
#define _PLS  (T_DEF | T_SET)
#define _AT   (T_AST)
#define _QU   (T_DEF | T_FNG | T_SHN)

/* Two combinations asked for often enough to be worth naming. */
#define _IDCH (T_IDC | T_DIG)
#define _META (T_SPC | T_DIP | T_MET | T_EOR)

extern char _ctype1[256];

/*
 * The tests themselves.
 *
 * Each is (c & QUOTE) == 0 first: a QUOTED character is never special, whatever it is.
 * In v7 that test did a second job -- QUOTE was bit 0200 OF THE CHARACTER, so the short
 * circuit was also the bounds check on a 128-entry table.  It is not any more: QUOTE is a
 * bit far above the byte (defs.h) and an eight-bit character reaches the subscript on its
 * own.  THE TABLES ARE 256 ENTRIES BECAUSE OF THAT, and shrinking them back reintroduces
 * an out-of-bounds read for every byte above 0177.
 *
 * NB THESE ARGS ARE NOT CALL BY VALUE !!!!  They are macros: an argument with a side
 * effect, such as *p++, is evaluated more than once.
 */
#define space(c)   (((c) & QUOTE) == 0 && _ctype1[c] & (T_SPC))
#define eofmeta(c) (((c) & QUOTE) == 0 && _ctype1[c] & (_META | T_EOF))
#define qotchar(c) (((c) & QUOTE) == 0 && _ctype1[c] & (T_QOT))
#define eolchar(c) (((c) & QUOTE) == 0 && _ctype1[c] & (T_EOR | T_EOF))
#define dipchar(c) (((c) & QUOTE) == 0 && _ctype1[c] & (T_DIP))
#define subchar(c) (((c) & QUOTE) == 0 && _ctype1[c] & (T_SUB | T_QOT))
#define escchar(c) (((c) & QUOTE) == 0 && _ctype1[c] & (T_ESC))

extern char _ctype2[256];

#define digit(c)    (((c) & QUOTE) == 0 && _ctype2[c] & (T_DIG))
#define fngchar(c)  (((c) & QUOTE) == 0 && _ctype2[c] & (T_FNG))
#define dolchar(c)  (((c) & QUOTE) == 0 && _ctype2[c] & (T_AST | T_BRC | T_DIG | T_IDC | T_SHN))
#define defchar(c)  (((c) & QUOTE) == 0 && _ctype2[c] & (T_DEF))
#define setchar(c)  (((c) & QUOTE) == 0 && _ctype2[c] & (T_SET))
#define digchar(c)  (((c) & QUOTE) == 0 && _ctype2[c] & (T_AST | T_DIG))
#define letter(c)   (((c) & QUOTE) == 0 && _ctype2[c] & (T_IDC))
#define alphanum(c) (((c) & QUOTE) == 0 && _ctype2[c] & (_IDCH))
#define astchar(c)  (((c) & QUOTE) == 0 && _ctype2[c] & (T_AST))

#endif // SH_CTYPE_H
