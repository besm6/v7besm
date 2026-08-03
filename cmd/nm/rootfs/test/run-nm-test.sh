#!/bin/sh
# Two nms built from one source must print the same symbol table.  Task C9c,
# ../../TODO.md.  Invoked by ctest as:
#
#	run-nm-test.sh HOSTNM SIM PROG AS AR SRCDIR NAME FIXTURE KIND [args...]
#
# HOSTNM is the freshly built host tool (build/cmd/nm/b6nm -- NOT an installed one, which
# would be a different version of the same source and would make this test say nothing).
# PROG is the native nm AS STAGED FOR THE DISK IMAGE, build/rootfs/usr/bin/nm, which is
# scripts/run-prog-test.sh's rule and holds here for the same reason: the bytes this runs
# are the bytes the kernel will run.
#
# NAME is this case's ctest name, FIXTURE the SRCDIR/FIXTURE*.s set it assembles, and KIND
# either `obj' (list the first object alone), `objs' (both, which is what turns on the
# file-name banner) or `archive' (put them in a library first, which is the only way to
# reach fgetarhdr() and the per-member loop).  NAME is separate from FIXTURE because one
# fixture is listed several ways, and each case gets a WORKING DIRECTORY OF ITS OWN named
# after NAME: cases writing one set of file names in one directory would race, ctest
# running them in parallel.
#
# THE INPUTS ARE ASSEMBLED AND ARCHIVED HERE, by the host b6as and b6ar, rather than
# checked in: a golden object would freeze the symbol-table layout at whatever it was the
# day it was generated, and this suite is the one that has to notice when it moves.
#
# WHY A LIVE DIFF AND NOT A CHECKED-IN .expected.  The property is agreement between two
# builds of ONE source, and an expectation cannot express it: the day someone changes how
# a class letter is chosen in ../nm.c, a live diff REQUIRES the change to land identically
# on both targets, where a checked-in file merely needs regenerating -- and whoever
# regenerates it from the host tool has quietly stopped testing the delta.  The
# b6_progtest cases beside this one are the other half: they pin the output itself, which
# two nms wrong in the same way would not.
#
# NO TWO SYMBOLS IN A FIXTURE MAY SHARE A SORT KEY.  qsort() is not stable and the two
# builds link DIFFERENT ONES -- the host's libc and ours (lib/libc/gen/qsort.c) -- so a
# repeated name under the default sort, or a repeated value under -n, comes out in one
# order here and another there and the diff fails on a difference that is not a defect.
# Real objects do have such ties (kernel/unix has seven `_str0's) and listing one is a
# fine manual check; it is not a case for this suite.
#
# STDOUT ONLY, and every input is named RELATIVELY: nm prints the file name in its banner
# and, under -o, on every line, so an absolute build-machine path would land in the diff.
# A diagnostic carries argv[0] -- "b6nm:" on one side and "nm:" on the other -- and can
# never be byte-identical, which is what the .expected cases beside this one are for.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostnm=$1
sim=$2
prog=$3
as=$4
ar=$5
srcdir=$6
name=$7
fixture=$8
kind=$9
shift 9

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

case $kind in
archive)
    "$ar" rc p.a $objs
    inputs=p.a
    ;;
objs)
    inputs=$objs
    ;;
*)
    inputs=p1.o
    ;;
esac

"$hostnm" "$@" $inputs > "$name.host"
env -i "$sim" "$prog" "$@" $inputs > "$name.native"

diff -u "$name.host" "$name.native"
