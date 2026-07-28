# The b6sim half of basename's cases -- which is the whole of them: this program touches no
# system state at all, reads no file, and answers on standard output, so there is nothing the
# booted kernel could say that the simulator cannot.  kernel/test/utils.sh runs it once
# anyway, for the same reason every other C2 program appears there: to prove the image's copy
# is the copy that works.
#
# suffix AND shortname ARE THE POINT.  Both go through the backwards comparison that used to
# be two char * relationals (basename.c's header, and ../../README.md §2) -- suffix down the
# matching path, shortname down the one where the name runs out first and v7 truncates to
# nothing.  A wrong fix for the hazard passes `plain' and fails these two.

b6_progtest(basename plain)
b6_progtest(basename nodir)
b6_progtest(basename root)
b6_progtest(basename suffix)
b6_progtest(basename nosuffix)
b6_progtest(basename shortname)
b6_progtest(basename noarg)
