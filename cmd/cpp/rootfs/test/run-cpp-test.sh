#!/bin/sh
# Two cpps built from one source must agree byte for byte.  Task C9a, ../TODO.md SS5.
# Invoked by ctest as:
#
#	run-cpp-test.sh HOSTCPP SIM PROG SRCDIR NAME [args...]
#
# HOSTCPP is the freshly built host tool (build/cmd/cpp/b6cpp -- NOT an installed one, which
# would be a different version of the same source and would make this test say nothing).
# PROG is the native cpp AS STAGED FOR THE DISK IMAGE, build/rootfs/usr/bin/cpp, which is
# scripts/run-prog-test.sh's rule and holds here for the same reason: the bytes this runs are
# the bytes the kernel will run.
#
# WHY A LIVE DIFF AND NOT A CHECKED-IN .expected.  The property is agreement between two
# builds of ONE source, and an expectation cannot express it: the day someone fixes a `##'
# corner in ../macro.c, a live diff REQUIRES the fix to land identically on both targets,
# where a checked-in file merely needs regenerating -- and whoever regenerates it from the
# host tool has quietly stopped testing the delta.  The b6_progtest cases beside this one are
# the other half: they pin the output itself, which two cpps wrong in the same way would not.
#
# STDOUT ONLY.  Not stdout+stderr as run-prog-test.sh captures, because a diagnostic here
# carries argv[0] -- "b6cpp:" on one side and "cpp:" on the other -- so stderr can never be
# byte-identical.  The BESM-6 build also warns where the host does not (MAXARGDEPTH, see
# ../defs.h), which is exactly a stderr difference that is not an output difference.
#
# THE FIXTURE IS COPIED INTO THE WORKING DIRECTORY and named relatively, because cpp PRINTS
# ITS INPUT'S PATH in every `# <line> "<file>"' marker it emits.  An absolute source path
# would land in the diff.  -I. reaches the copied local header the same way for both runs.
#
# NO ANGLE-BRACKET INCLUDE MAY APPEAR IN A FIXTURE: ../cpp.c pushes the literal
# "/usr/include" onto the search path, and under b6sim that is the BUILD MACHINE's
# (../../README.md SS9).  And no __DATE__/__TIME__: they differ between the two runs.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostcpp=$1
sim=$2
prog=$3
srcdir=$4
name=$5
shift 5

cp "$srcdir/$name.c" .
for h in "$srcdir"/*.h; do
    [ -e "$h" ] && cp "$h" .
done

"$hostcpp" "$@" -I. "$name.c" > "$name.host" 2>/dev/null || true
env -i "$sim" "$prog" "$@" -I. "$name.c" > "$name.native" 2>/dev/null || true

diff -u "$name.host" "$name.native"
