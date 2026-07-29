/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The shell's own character tables -- not <ctype.h>'s, and not named like them.
//
// 256 ENTRIES EACH, and v7 had 128.  The extra half is all zeroes and buys nothing but
// safety, which is the point: v7 could stop at 128 because the quoting mark was bit 0200
// OF THE CHARACTER, so the (c & QUOTE) == 0 test in front of every subscript in ctype.h
// doubled as the bounds check -- no character with that bit set ever reached a table.
// Here the mark is a byte of its own (defs.h) and `char' is unsigned, so a Cyrillic byte
// arrives as an ordinary subscript in 0200..0377 and the guard says nothing about it.
// Nothing above 0177 is special to the shell, so the upper half is zero, but it has to
// EXIST.  126 words, and the alternative is a range test at seventeen call sites.
//
#include "defs.h"

//
// Table 1: what each character means to the LEXER.  One entry per character, holding the
// T_* bits from ctype.h -- is it a space, does it end a word, does it start a quote, is
// it one of ; & | < >.  A table lookup rather than a chain of comparisons, because
// word() asks about every character it reads.
//
char _ctype1[256] = {
    /*	000	001	002	003	004	005	006	007	*/
    _EOF, 0, 0, 0, 0, 0, 0, 0,

    /*	bs	ht	nl	vt	np	cr	so	si	*/
    0, _TAB, _EOR, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,

    /*	sp	!	"	#	$	%	&	'	*/
    _SPC, 0, _DQU, 0, _DOL1, 0, _AMP, 0,

    /*	(	)	*	+	,	-	.	/	*/
    _BRA, _KET, 0, 0, 0, 0, 0, 0,

    /*	0	1	2	3	4	5	6	7	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	8	9	:	;	<	=	>	?	*/
    0, 0, 0, _SEM, _LT, 0, _GT, 0,

    /*	@	A	B	C	D	E	F	G	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	H	I	J	K	L	M	N	O	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	P	Q	R	S	T	U	V	W	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	X	Y	Z	[	\	]	^	_	*/
    0, 0, 0, 0, _BSL, 0, _HAT, 0,

    /*	`	a	b	c	d	e	f	g	*/
    _LQU, 0, 0, 0, 0, 0, 0, 0,

    /*	h	i	j	k	l	m	n	o	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	p	q	r	s	t	u	v	w	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	x	y	z	{	|	}	~	del	*/
    0, 0, 0, 0, _BAR, 0, 0, 0

    // 0200..0377 are left to the zero fill: nothing above 0177 is special to the lexer.
};

//
// Table 2: what each character means to $ SUBSTITUTION and to filename matching -- is it
// a digit, a letter, one of the * ? [ wildcards, one of the - = + ? that follow a name
// inside ${...}.
//
char _ctype2[256] = {
    /*	000	001	002	003	004	005	006	007	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    /*	bs	ht	nl	vt	np	cr	so	si	*/
    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,

    /*	sp	!	"	#	$	%	&	'	*/
    0, _PCS, 0, _NUM, _DOL2, 0, 0, 0,

    /*	(	)	*	+	,	-	.	/	*/
    0, 0, _AST, _PLS, 0, _MIN, 0, 0,

    /*	0	1	2	3	4	5	6	7	*/
    _DIG, _DIG, _DIG, _DIG, _DIG, _DIG, _DIG, _DIG,

    /*	8	9	:	;	<	=	>	?	*/
    _DIG, _DIG, 0, 0, 0, _EQ, 0, _QU,

    /*	@	A	B	C	D	E	F	G	*/
    _AT, _UPC, _UPC, _UPC, _UPC, _UPC, _UPC, _UPC,

    /*	H	I	J	K	L	M	N	O	*/
    _UPC, _UPC, _UPC, _UPC, _UPC, _UPC, _UPC, _UPC,

    /*	P	Q	R	S	T	U	V	W	*/
    _UPC, _UPC, _UPC, _UPC, _UPC, _UPC, _UPC, _UPC,

    /*	X	Y	Z	[	\	]	^	_	*/
    _UPC, _UPC, _UPC, _SQB, 0, 0, 0, _UPC,

    /*	`	a	b	c	d	e	f	g	*/
    0, _LPC, _LPC, _LPC, _LPC, _LPC, _LPC, _LPC,

    /*	h	i	j	k	l	m	n	o	*/
    _LPC, _LPC, _LPC, _LPC, _LPC, _LPC, _LPC, _LPC,

    /*	p	q	r	s	t	u	v	w	*/
    _LPC, _LPC, _LPC, _LPC, _LPC, _LPC, _LPC, _LPC,

    /*	x	y	z	{	|	}	~	del	*/
    _LPC, _LPC, _LPC, _CBR, 0, _CKT, 0, 0

    // 0200..0377 are left to the zero fill.  A Cyrillic byte is deliberately NOT a letter
    // here: T_IDC decides what may appear in a $name, and a variable's name is an
    // identifier in the v7 sense.  It is the variable's VALUE that had to become
    // eight-bit clean, not its name.
};
