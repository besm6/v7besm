#!/bin/sh
# Two linkers built from one source must produce the same image, byte for byte.
# Task C9b, ../../README.md.  Invoked by ctest as:
#
#	run-ld-test.sh HOSTLD SIM PROG NM AS SRCDIR NAME FIXTURE [ldargs...]
#
# NAME is this case's ctest name and FIXTURE the SRCDIR/FIXTURE*.s it links.  They are
# separate because the same fixture is linked several ways (-r, -x, -n), and each case gets
# a WORKING DIRECTORY OF ITS OWN named after NAME: the scratch objects and the two output
# images would otherwise be four cases writing one set of file names in one directory, and
# ctest runs them in parallel.
#
# HOSTLD is the freshly built host tool (build/cmd/ld/b6ld -- NOT an installed one, which
# would be a different version of the same source and would make this test say nothing).
# PROG is the native ld AS STAGED FOR THE DISK IMAGE, build/rootfs/usr/bin/ld, which is
# scripts/run-prog-test.sh's rule and holds here for the same reason: the bytes this runs
# are the bytes the kernel will run.
#
# THE INPUTS ARE ASSEMBLED HERE, by the host b6as, from NAME1.s/NAME2.s in SRCDIR.  Checking
# in a pair of .o files instead would freeze the object format at whatever it was the day
# they were generated, and this suite is the one that has to notice when it moves.
#
# WHY A LIVE COMPARISON AND NOT A CHECKED-IN IMAGE: the property is agreement between two
# builds of ONE source.  The day someone changes how relocate_halfword() folds a short
# address, this REQUIRES the change to land identically on both targets, where a golden file
# merely needs regenerating -- and whoever regenerates it from the host tool has quietly
# stopped testing the delta.  The b6_progtest cases beside this one are the other half: they
# pin the diagnostics, which two linkers wrong in the same way would not.
#
# THE OUTPUT IS COMPARED, NOT THE STDOUT: ld writes a binary a.out and says nothing.  cmp,
# then b6nm through the diff so a failure names the symbol that moved rather than a byte
# offset nobody can read.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostld=$1
sim=$2
prog=$3
nm=$4
as=$5
srcdir=$6
name=$7
fixture=$8
shift 8

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

# Assemble the parts.  Relative names, so nothing records a build-machine path.
n=1
objs=
for s in "$srcdir/$fixture"*.s; do
    cp "$s" "p$n.s"
    "$as" -o "p$n.o" "p$n.s"
    objs="$objs p$n.o"
    n=$((n + 1))
done

"$hostld" "$@" -o "$name.host" $objs
env -i "$sim" "$prog" "$@" -o "$name.native" $objs

if cmp -s "$name.host" "$name.native"; then
    exit 0
fi

# Differ: say what moved before failing on the bytes.
echo "images differ: $name"
"$nm" -n "$name.host" > "$name.host.nm" 2>&1 || true
"$nm" -n "$name.native" > "$name.native.nm" 2>&1 || true
diff -u "$name.host.nm" "$name.native.nm" || true
cmp -l "$name.host" "$name.native" | head -20
exit 1
