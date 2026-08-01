# tr's b6sim cases -- task C5b.  No fixture and no file: tr opens nothing, so every case is
# a <case>.in fed to the program and a <case>.expected diffed against what comes back.
#
# `utf8' IS THE ONE THAT SAYS SOMETHING NEW.  v7's tr is eight-bit clean by construction --
# its tables are [256] and code[i] defaults to i -- so `tr a-z A-Z' passes a Cyrillic line
# through UNTOUCHED while folding the ASCII beside it.  That is not a divergence and not a
# fix; it is the property §11 wants and the only program of C5b's seven that had it already.
# The case exists because nothing else in this directory would notice if a later change
# masked a byte to 0177, which is what the other six had to have taken out of them.
#
# `escape' IS THE ONE MASK THAT MATTERS.  `\ooo' takes up to three octal digits, so nextc()
# can return 511 and its `&0377' is the only one of the file's eight that is not a no-op --
# see tr.c's header.  This case exercises the escape path; a `\777' would exercise the mask
# itself, but v7 gives no way to observe which of code[511] and code[255] was written, so the
# assertion that guards it is the 256-entry table and the comment, not a case.
#
# `noargs' is the identity copy, which is also where a NUL would be dropped if the input had
# one -- a v7 semantic tr.1 states and this port keeps.
#
# Every .expected here was computed by hand from the table tr builds, not captured from a
# first run, which for six short cases is checkable by eye.

b6_progtest(tr upper)      # the ordinary transliteration, a-z to A-Z
b6_progtest(tr utf8)       # bytes above 0177 pass through untouched (§11)
b6_progtest(tr squeeze)    # -s, and the spaces it must NOT squeeze
b6_progtest(tr complement) # -c with -d: keep the digits, delete everything else
b6_progtest(tr escape)     # \012 as a set member
b6_progtest(tr noargs)     # no sets at all: an identity copy
