#!/bin/sh
# Two assemblers built from one source must produce the same object, byte for byte.
# Task C9b, ../../README.md.  Invoked by ctest as:
#
#	run-as-test.sh HOSTAS SIM PROG SRCDIR NAME [args...]
#
# HOSTAS is the freshly built host tool (build/cmd/as/b6as -- NOT an installed one, which
# would be a different version of the same source and would make this test say nothing).
# PROG is the native as AS STAGED FOR THE DISK IMAGE, build/rootfs/usr/bin/as, which is
# scripts/run-prog-test.sh's rule and holds here for the same reason: the bytes this runs
# are the bytes the kernel will run.
#
# WHY A LIVE COMPARISON AND NOT A CHECKED-IN OBJECT.  The property is agreement between two
# builds of ONE source, and a checked-in a.out cannot express it: the day someone changes
# an opcode encoding in ../../tables.c, this REQUIRES the change to land identically on both
# targets, where a golden file merely needs regenerating -- and whoever regenerates it from
# the host tool has quietly stopped testing the delta.  The b6_progtest cases beside this
# one are the other half: they pin the diagnostics, which two assemblers wrong in the same
# way would not.
#
# THE OBJECT IS COMPARED, NOT THE OUTPUT: as writes a binary a.out and says nothing on
# stdout.  cmp, then b6nm/b6disasm through the diff so a failure names the symbol or the
# instruction that moved rather than a byte offset nobody can read.
#
# THE FIXTURE IS COPIED INTO THE WORKING DIRECTORY and named relatively, because a "# N
# \"file\"" line marker in the source records a path that reaches the symbol table -- an
# absolute build-machine path would differ between a plain run and one under b6sim.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostas=$1
sim=$2
prog=$3
nm=$4
srcdir=$5
name=$6
shift 6

cp "$srcdir/$name.s" .

"$hostas" "$@" -o "$name.host" "$name.s"
env -i "$sim" "$prog" "$@" -o "$name.native" "$name.s"

if cmp -s "$name.host" "$name.native"; then
    exit 0
fi

# Differ: say what moved before failing on the bytes.
echo "objects differ: $name"
"$nm" -n "$name.host" > "$name.host.nm" 2>&1 || true
"$nm" -n "$name.native" > "$name.native.nm" 2>&1 || true
diff -u "$name.host.nm" "$name.native.nm" || true
cmp -l "$name.host" "$name.native" | head -20
exit 1
