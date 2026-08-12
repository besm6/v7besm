# b6sim cases for at(1) -- task C21.  ALL EIGHT ARE DIAGNOSTICS, and that is a limit of the
# harness rather than a choice: every path that gets past makeuday() opens an absolute
# /usr/spool/at/... which under b6sim is the BUILD MACHINE's (../../README.md §9).  So there
# is no b6sim case that spools anything, and the positive half of at lives in
# kernel/test/multi.  ../README.md, "What this harness cannot say".
#
# The last three call time()/localtime() first -- the host's clock under b6sim -- but nothing
# in what they print depends on it.
b6_progtest(at argcount)

# The suffix switch's default arm, and the `:' arm, which prints a colon of its own.
b6_progtest(at badtime)
b6_progtest(at badcolon)

# The two range gates.  1370 passes the first (it is below 2400) and fails the second: the
# minute field is checked by remainder, the number being base 100 rather than base 60.
b6_progtest(at range)
b6_progtest(at minute)

# `j' is january, june and july; `t' is tuesday and thursday, and matches no month at all,
# which is what makes it reach the second table.
b6_progtest(at ambigmonth)
b6_progtest(at ambigday)

b6_progtest(at badday)
