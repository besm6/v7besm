# b6sim cases for dc(1) -- task C15.  Each is a <case>.in fed to the program AS STAGED FOR THE
# IMAGE, with its combined output diffed against <case>.expected.  ../../ed/test is the model:
# dc's whole command language arrives on standard input, so .in is the vehicle and .args is
# used only by the two cases that name a file.
#
# EVERY CASE EXITS 0 BUT ONE.  readc() calls exit(0) at end of input and every diagnostic is a
# `continue', so nofile -- which cannot open its argument -- is the only .status file here.
#
# A STRING IS A BLOCK WITH A BYTE ABOVE 99 IN IT, which decides how half these cases are
# written.  dc gives a block no type: `p' calls it a string if some cell exceeds 99 and a
# number otherwise, so `[abc]p' prints a number (strascii pins that; it is v7's own wart and is
# kept).  Any case whose macro must PRINT its name therefore spells it with a letter from `d'
# on -- [lt], [gt], [eq] -- and never [a].  README.md has the account, and strutf8p is the
# divergence: an eight-bit byte reads as a string here and as a number in v7.
#
# THE ORDER OF A RELATION IS THE TOP AGAINST THE SECOND, not the other way about: `2 1 <l'
# executes l and `1 2 <l' does not, which cond and negcmp pin because it reads backwards.
#
# A CASE THAT CHANGES THE INPUT BASE POISONS EVERY LATER LINE IN THE SAME FILE -- after `16i'
# the literal 16 is 22 -- so ibase, ibase2 and ibase8 are three files rather than three lines.
#
# WHAT CANNOT BE ASSERTED HERE, and is checked by hand instead (README.md, "What this harness
# cannot say"): `!' and the shell escape, since b6sim cannot exec /bin/sh; a delivered SIGINT
# and the setjmp unwind; that a `P' or a prompt reaches the terminal before dc blocks, which is
# a matter of timing and not of content; and the garbage()/ospace() arena compaction.

# Arithmetic.  bigmul is the §9 invariant oracle -- x y * y / x - is 0 whatever the numbers --
# and is the only check on forty-digit multiplication a reviewer cannot do by eye.
b6_progtest(dc add)
b6_progtest(dc sub)
b6_progtest(dc mul)
b6_progtest(dc divint)         # all four sign combinations; the quotient truncates toward zero
b6_progtest(dc mod)            # the remainder takes the dividend's sign
b6_progtest(dc carrychain)     # carry through every cell of a twenty-digit number
b6_progtest(dc bignum)         # sixty digits in and out
b6_progtest(dc bigmul)
b6_progtest(dc divzero)        # and that only the DIVISOR comes back, which is v7's
b6_progtest(dc scalezero)      # `0i' then a fraction: v7 divided by a zero base and died here

# Scale.
b6_progtest(dc scalek)
b6_progtest(dc scaledv)        # ten, twenty and fifty places of 1/7, 355/113 and 1/97
b6_progtest(dc scalemul)       # the sum-of-scales cap
b6_progtest(dc scaleadd)       # eqk() aligning two unequal scales
b6_progtest(dc scalebad)

# Bases.  obasefrac is the case that pins log2v(): v7 counted down from the top bit of a
# 32-bit long, and on 41 bits the fraction digit count came out negative.
b6_progtest(dc ibase)
b6_progtest(dc ibase2)
b6_progtest(dc ibase8)
b6_progtest(dc obasehex)       # a base of 16 or less takes the hexot path
b6_progtest(dc obasebig)       # and anything larger takes bigot, with fw/fw1/ll
b6_progtest(dc obaseone)
b6_progtest(dc obasetwo)
b6_progtest(dc obasefrac)
b6_progtest(dc basesio)
b6_progtest(dc hexdigit)

# Negatives, which is what the sign-extending cell readers are for.
b6_progtest(dc neg)
b6_progtest(dc negprint)       # base 10 and base 16
b6_progtest(dc negprint2)      # and base 100, a third print path
b6_progtest(dc negcmp)
b6_progtest(dc chsignedge)     # chsign()'s 99 and truncate branches, and _0
b6_progtest(dc negerr)
b6_progtest(dc negindex)
b6_progtest(dc negexp)

# Square root and exponent.
b6_progtest(dc sqroot)
b6_progtest(dc sqrtinv)        # v v * back to the argument, within the scale
b6_progtest(dc expncase)
b6_progtest(dc experr)
b6_progtest(dc expbig)

# Registers, arrays, strings and macros.
b6_progtest(dc regs)
b6_progtest(dc regstack)
b6_progtest(dc regmissing)
b6_progtest(dc regutf8)        # a register name is ONE BYTE, so a Cyrillic letter is two
b6_progtest(dc array)
b6_progtest(dc arrayunset)     # an unset element is 0.  v7 pushed PTRSZ nulls, which prints
                               # as one 0 only because a PDP-11 pointer is two bytes
b6_progtest(dc arraygrow)      # the copy() that widens an array, and the nalloc() over-read
b6_progtest(dc arraybig)       # index 2047: 2,053 words, about a tenth of the heap
b6_progtest(dc arrayovf)
b6_progtest(dc string)
b6_progtest(dc stringp)        # `P' writes no newline, which is why this is its own case
b6_progtest(dc stringempty)
b6_progtest(dc stringnest)     # `[' inside a string is counted, so the brackets nest
b6_progtest(dc strascii)       # v7's wart, kept: no byte of `abc' exceeds 99
b6_progtest(dc strutf8)
b6_progtest(dc strutf8p)       # the divergence
b6_progtest(dc macro)          # the manual page's own factorial
b6_progtest(dc cond)           # all six outcomes of < > =

# Control, stack and errors.
b6_progtest(dc stackops)
b6_progtest(dc zlen)
b6_progtest(dc xlen)
b6_progtest(dc stackempty)     # `p' with nothing: `empty stack'
b6_progtest(dc stackempty2)    # an operator with one: `stack empty'.  v7 has both spellings
b6_progtest(dc stackfull)      # v7 diagnosed one push after writing past the array
b6_progtest(dc nestdepth)      # and one nesting level after writing past that one.  A TAIL
                               # CALL REPLACES THE FRAME rather than nesting, so the macro
                               # here has a byte after the `x' -- `[lax]sa lax' never nests
b6_progtest(dc quit)
b6_progtest(dc quitq)
b6_progtest(dc quitneg)
b6_progtest(dc unimpl)
b6_progtest(dc unimpl8)        # 0377 is ignored; 0376 is reported
b6_progtest(dc unimplmacro)    # the same byte out of a MACRO, where sgetc sign-extends it:
                               # unmasked it printed a 41-bit octal and disagreed with unimpl8

# The file argument.
b6_progtest(dc readfile)       # runs the file, then falls through to standard input
b6_progtest(dc nofile)         # the only .status here.  A RELATIVE name: the source directory
                               # differs per machine and the diagnostic prints what it was given
