#!/bin/sh
# Two sizes built from one source must print the same six numbers.  Task C9c,
# ../../README.md.  Invoked by ctest as:
#
#	run-size-test.sh HOSTSIZE SIM PROG AS SRCDIR NAME FIXTURE [args...]
#
# HOSTSIZE is the freshly built host tool (build/cmd/size/b6size -- NOT an installed one,
# which would be a different version of the same source and would make this test say
# nothing).  PROG is the native size AS STAGED FOR THE DISK IMAGE,
# build/rootfs/usr/bin/size, which is scripts/run-prog-test.sh's rule and holds here for
# the same reason: the bytes this runs are the bytes the kernel will run.
#
# NAME is this case's ctest name and FIXTURE the SRCDIR/FIXTURE.s it assembles.  They are
# separate because the same fixture is measured several ways (bytes, then -w), and each
# case gets a WORKING DIRECTORY OF ITS OWN named after NAME: two cases writing one set of
# file names in one directory would race, ctest running them in parallel.
#
# THE INPUT IS ASSEMBLED HERE, by the host b6as, rather than checked in as a .o: a golden
# object would freeze the header layout at whatever it was the day it was generated, and
# this suite is the one that has to notice when it moves.
#
# WHY A LIVE DIFF AND NOT A CHECKED-IN .expected.  The property is agreement between two
# builds of ONE source, and an expectation cannot express it: the day someone changes how
# a_bss is computed, a live diff REQUIRES the change to land identically on both targets,
# where a checked-in file merely needs regenerating -- and whoever regenerates it from the
# host tool has quietly stopped testing the delta.  The b6_progtest cases beside this one
# are the other half: they pin the output itself, which two sizes wrong in the same way
# would not.
#
# STDOUT ONLY, and the object is named RELATIVELY: size prints the file name in its last
# column, so an absolute build-machine path would land in the diff.  A diagnostic carries
# argv[0] -- "b6size:" on one side and "size:" on the other -- and can never be
# byte-identical, which is what the .expected cases beside this one are for.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostsize=$1
sim=$2
prog=$3
as=$4
srcdir=$5
name=$6
fixture=$7
shift 7

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

cp "$srcdir/$fixture.s" p.s
"$as" -o p.o p.s

"$hostsize" "$@" p.o > "$name.host"
env -i "$sim" "$prog" "$@" p.o > "$name.native"

diff -u "$name.host" "$name.native"
