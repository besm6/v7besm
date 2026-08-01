# col's b6sim cases -- task C5b.  No fixture and no file: col opens nothing at all, which is
# also the correction ../TODO.md wants -- it lists col as needing the file-oracle harness
# tee(1) and split(1) use, and col writes only to standard output.
#
# `utf8' IS THE DIVERGENCE AND IT IS THE POINT OF THIS DIRECTORY.  v7's col steals bit 0200
# of every stored character for the Model 37 Teletype's Greek half-shift and masks its input
# with `c &= 0177', then admits only `c > 040 && c < 0177' to the page.  Run v7's col over
# `привет' and it prints `P?QP8P2P5Q' -- twelve bytes masked down to ten of plausible ASCII,
# with the two that fell below a space dropped.  That is the failure worth having a case for:
# not an empty line and not a diagnostic, but a wrong answer shaped like a right one.  This
# col prints the line back, because the shift is gone and a byte above 0177 is an ordinary
# printable.  The case is the whole assertion for README.md's argument, and checkable by eye.
#
# `revlf' IS THE ONE THAT EXERCISES WHAT col IS FOR.  The other cases would pass with the
# page ring stubbed out to a straight copy; this one feeds two lines, backs up over both with
# a reverse line feed (013), writes into columns the first line left empty, and requires the
# two to come back MERGED into one.  It is store(), fetch(), decr() and the column walk in
# outc() in a single eight-byte fixture.
#
# `overstrike' AND `bflag' ARE THE TWO HALVES OF ONE SWITCH: the same input, with and without
# -b, and they differ in what col does when a column is already occupied -- open two bytes and
# keep both, or replace.  Testing either alone would leave the branch half covered.
#
# `tabs' AND `xflag' ARE THE OTHER SUCH PAIR, over emit()'s tab reconstruction: eight spaces
# come back as one tab, or as eight spaces under -x.  Eight and not seven because the
# reconstruction fires on `(++ncp & 7) == 0'.
#
# WHAT IS NOT HERE, AND WHY: the two bad-option diagnostics.  col prints them through
# argv[0], and under b6sim argv[0] is the STAGED PATH of the program -- an absolute build
# directory -- so a checked-in .expected would hold one machine's layout.  That is
# ../../README.md §9's `argv[0] is the staged path' limit, the same one that stops test(1)
# being driven through both its names here.  They are asserted under the booted kernel
# instead, in kernel/test/filters, where the shell passes argv[0] as it was typed.
#
# Every .expected here was computed by walking the program's page ring by hand, not captured
# from a first run.

b6_progtest(col plain)      # no control sequences at all: the text back unchanged
b6_progtest(col revlf)      # 013 backs up over two lines and the two come back merged
b6_progtest(col overstrike) # a column already occupied: keep both, with the backspace
b6_progtest(col bflag)      # -b: the same input, keeping only the last
b6_progtest(col tabs)       # emit() puts a tab back where eight columns of space were
b6_progtest(col xflag)      # -x: the same input, left as spaces
b6_progtest(col utf8)       # THE DIVERGENCE: v7's col prints nothing here (see above)
