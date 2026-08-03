#!/bin/sh
# Two disassemblers built from one source must decode a file the same way.  Task C9c,
# ../../TODO.md.  Invoked by ctest as:
#
#	run-disasm-test.sh HOSTDISASM SIM PROG AS LD SRCDIR NAME FIXTURE KIND [args...]
#
# HOSTDISASM is the freshly built host tool (build/cmd/disasm/b6disasm -- NOT an installed
# one, which would be a different version of the same source and would make this test say
# nothing).  PROG is the native disasm AS STAGED FOR THE DISK IMAGE,
# build/rootfs/usr/bin/disasm, which is scripts/run-prog-test.sh's rule and holds here for
# the same reason: the bytes this runs are the bytes the kernel will run.
#
# NAME is this case's ctest name, FIXTURE the SRCDIR/FIXTURE.s it assembles, and KIND
# either `obj' (stop at the relocatable object) or `image' (link it as well).  The two
# kinds are not interchangeable: -r REQUIRES relocation records, which a linked image does
# not have -- RELFLG marks their absence -- so the relocation cases must run on the object
# and the image cases must not ask for them.  NAME is separate from FIXTURE because one
# fixture is decoded several ways, and each case gets a WORKING DIRECTORY OF ITS OWN named
# after NAME: cases writing one set of file names in one directory would race, ctest
# running them in parallel.
#
# THE INPUT IS ASSEMBLED HERE, by the host b6as and b6ld, rather than checked in as a .o:
# a golden object would freeze the instruction encoding at whatever it was the day it was
# generated, and this suite is the one that has to notice when it moves.
#
# WHY A LIVE DIFF AND NOT A CHECKED-IN .expected.  The property is agreement between two
# builds of ONE source, and an expectation cannot express it: the day someone adds a
# mnemonic to ../dis.c's tables, a live diff REQUIRES the addition to land identically on
# both targets, where a checked-in file merely needs regenerating -- and whoever
# regenerates it from the host tool has quietly stopped testing the delta.  The
# b6_progtest cases beside this one are the other half: they pin the output itself, which
# two disassemblers wrong in the same way would not.
#
# STDOUT ONLY.  A diagnostic carries argv[0] -- "b6disasm:" on one side and "disasm:" on
# the other -- so stderr can never be byte-identical, which is what the .expected cases
# beside this one are for.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostdisasm=$1
sim=$2
prog=$3
as=$4
ld=$5
srcdir=$6
name=$7
fixture=$8
kind=$9
shift 9

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

cp "$srcdir/$fixture.s" p.s
"$as" -o p.o p.s
if [ "$kind" = image ]; then
    "$ld" -o p.out p.o
    input=p.out
else
    input=p.o
fi

"$hostdisasm" "$@" "$input" > "$name.host"
env -i "$sim" "$prog" "$@" "$input" > "$name.native"

diff -u "$name.host" "$name.native"
