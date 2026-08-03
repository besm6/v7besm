#!/bin/sh
# Two strips built from one source must leave the same object behind, byte for byte.
# Task C9c, ../../TODO.md.  Invoked by ctest as:
#
#	run-strip-test.sh HOSTSTRIP SIM PROG AS NM SRCDIR NAME FIXTURE [args...]
#
# HOSTSTRIP is the freshly built host tool (build/cmd/strip/b6strip -- NOT an installed
# one, which would be a different version of the same source and would make this test say
# nothing).  PROG is the native strip AS STAGED FOR THE DISK IMAGE,
# build/rootfs/usr/bin/strip, which is scripts/run-prog-test.sh's rule and holds here for
# the same reason: the bytes this runs are the bytes the kernel will run.
#
# THE FIXTURE IS COPIED TWICE and each copy stripped in place.  strip has no -o: it
# rewrites the file it is handed, so unlike as and ld it cannot be pointed at two output
# names, and the comparison is between two copies of one input rather than between two
# outputs of one run.  Each case gets a WORKING DIRECTORY OF ITS OWN named after NAME,
# ctest running the cases in parallel.
#
# THE INPUT IS ASSEMBLED HERE, by the host b6as, rather than checked in as a .o: a golden
# object would freeze the header layout at whatever it was the day it was generated, and
# this suite is the one that has to notice when it moves.
#
# WHY A LIVE COMPARISON AND NOT A CHECKED-IN OBJECT.  The property is agreement between
# two builds of ONE source, and a golden file cannot express it: the day someone changes
# which header fields survive a strip, this REQUIRES the change to land identically on
# both targets, where a golden file merely needs regenerating -- and whoever regenerates it
# from the host tool has quietly stopped testing the delta.  The b6_progtest cases beside
# this one are the other half: they pin the diagnostics, which two strips wrong in the
# same way would not.
#
# THE OBJECT IS COMPARED, NOT THE OUTPUT: strip says nothing on stdout.  cmp, then b6nm
# through the diff so a failure names what survived rather than a byte offset nobody can
# read -- though on a stripped pair that listing is expected to be empty on both sides,
# which is itself the thing being asserted.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hoststrip=$1
sim=$2
prog=$3
as=$4
nm=$5
srcdir=$6
name=$7
fixture=$8
shift 8

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

cp "$srcdir/$fixture.s" p.s
"$as" -o p.o p.s
cp p.o "$name.host"
cp p.o "$name.native"

"$hoststrip" "$@" "$name.host"
env -i "$sim" "$prog" "$@" "$name.native"

if cmp -s "$name.host" "$name.native"; then
    exit 0
fi

# Differ: say what survived before failing on the bytes.
echo "stripped objects differ: $name"
"$nm" -n "$name.host" > "$name.host.nm" 2>&1 || true
"$nm" -n "$name.native" > "$name.native.nm" 2>&1 || true
diff -u "$name.host.nm" "$name.native.nm" || true
cmp -l "$name.host" "$name.native" | head -20
exit 1
