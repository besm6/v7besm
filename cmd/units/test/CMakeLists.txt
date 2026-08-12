# b6sim cases for units(1) -- task C18.  Each is a <case>.in fed to the program AS STAGED FOR
# THE IMAGE, with its combined output diffed against <case>.expected.  ../../ed/test is the
# model: units takes its whole dialogue on standard input, so .in is the vehicle.
#
# EVERY CASE NAMES ITS TABLE, and that is ../../README.md SS9 applied rather than a style
# choice -- ../../look/test/CMakeLists.txt says it first and for the same reason.  units'
# default is the absolute path /usr/lib/units, and under b6sim the system calls are the HOST's,
# so that path is the BUILD MACHINE's.  units takes the file as argv[1], so every .args below
# passes one: @srcdir@/../units for the real table, @srcdir@/tiny.units for the fixture.
#
# THE FIRST LINE OF EVERY EXPECTATION IS THE TABLE'S OWN SIZE -- `421 units; 3941 bytes' --
# which is the assertion that the six commented-out definitions are still commented out and
# that nothing else moved.
#
# WHAT THIS HARNESS CANNOT SAY, and is checked by hand instead (README.md, "What this harness
# cannot say"): the bare `units' with no argument, which reads the real /usr/lib/units off the
# image; and SIGFPE arriving at all, the gates below meaning it never should.

b6_progtest(units inch)        # units.1's own example, to the last printed digit
b6_progtest(units power)       # a multiplier, a `/' and the `2' exponent suffix
b6_progtest(units frac)        # the 1|180 form, in the table and typed
b6_progtest(units prefix)      # micro/mega/kilo, and the meg-for-mega alias
b6_progtest(units plural)      # the trailing-`s' retry
b6_progtest(units conform)     # `conformability', and the two dimension dumps under it
b6_progtest(units unknown)     # `cannot recognize' -- and `mole' is now one of those
b6_progtest(units range)       # THE GATES: a typed 1+30 and 1-30 report rather than fault
b6_progtest(units outofrange)  # a TABLE that overflows: diagnosed at load, the name dropped
b6_progtest(units dump)        # the undocumented `-' flag, over the fixture
b6_progtest(units nofile)      # `no table', the one case that exits nonzero

# tiny.units is named through @srcdir@ and so needs no copy; it is the only fixture here.
