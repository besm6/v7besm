# b6sim cases for /bin/cal -- task C5f.  ../../README.md §9 is the harness; the files are
# <case>.args and <case>.expected, and run-prog-test.sh diffs stdout and stderr together.
#
# NO FIXTURES AND NO STANDARD INPUT: cal reads nothing.  Its whole input is two numbers and
# its whole output is a function of them, which makes it the one program of task C5f whose
# oracle needs no design at all.
#
# THE SECOND OPINION IS PYTHON'S calendar MODULE, and it is worth having even though these
# expectations are readable by eye.  Every day of every month of 1753, 1900, 1970, 2000,
# 2026, 2100 and 9999 -- 84 months -- was compared against `calendar.weekday(y, m, d)' by
# cell position, and all 84 agree.  That covers jan1()'s three corrections in both
# directions: the ordinary leap year (2000, which divides by 400), the century that is not
# one (1900 and 2100), and both ends of the range.
#
# HOST cal IS NOT AN ORACLE HERE, and the reason is the heading rather than the arithmetic:
# BSD writes `Su Mo Tu We Th Fr Sa' where v7 writes ` S  M Tu  W Th  F  S', and pads its
# lines with trailing blanks.  The DAY CELLS agree, September 1752 included, which is the
# one part Python cannot check.

b6_progtest(cal changeover)   # THE one output of this program everybody checks: September
                              # 1752 runs 1, 2, 14, 15 ... 30, the eleven days Britain lost
b6_progtest(cal yearold)      # ... and the whole of 1752, so the short September is laid out
                              # beside two ordinary months rather than on its own
b6_progtest(cal leap)         # February 2000: divides by 400, so it HAS a 29th
b6_progtest(cal noleap)       # February 1900: divides by 100 and not by 400, so it has not
b6_progtest(cal century)      # February 1600, the other side of the same rule
b6_progtest(cal month)        # an ordinary month
b6_progtest(cal year)         # the twelve-month layout: three months across, six rows down
b6_progtest(cal yearone)      # year 1, the bottom of the range
b6_progtest(cal usage)        # no arguments at all
b6_progtest(cal badmonth)     # month 13
b6_progtest(cal badzero)      # month 0
b6_progtest(cal badyear)      # year 0
b6_progtest(cal badtext)      # a month that is not a number: number() answers 0
b6_progtest(cal bigyear)      # year 10000, the top of the range
