# The b6sim half of time's cases, and there is exactly one of them.
#
# EVERYTHING THIS PROGRAM DOES IS UNREPRODUCIBLE HERE, twice over.  It forks and execs its
# argument, and no BESM-6 a.out exists at any path on the build machine, so nothing it could
# be asked to run would load.  And it reports through times(2), which b6sim answers out of the
# HOST's ::times() in the host's own clock units -- not this machine's 250 Hz, and not the
# same twice.  ../TODO.md said as much before the port started: "all five except time test
# cleanly under b6sim".
#
# What is left is the one path that returns before any of that: with no command to run, v7's
# time exits 0 and prints nothing.  It is worth a case because it is the guard the program
# opens with, and because it proves the staged binary loads and runs at all.
#
# The measurement itself is kernel/test/utils.sh's, with the three reported intervals masked
# by run-utils.sh and the exit status -- the child's, shifted down -- diffed as it stands.

b6_progtest(time noargs)
