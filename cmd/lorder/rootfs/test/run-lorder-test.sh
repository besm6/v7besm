#!/bin/sh
# The staged /usr/bin/lorder must print what the host b6lorder prints, run BY THE BESM-6
# SHELL over the BESM-6 nm, sed, sort, join and basename.  Task C9c, ../../README.md.
# Invoked by ctest as:
#
#	run-lorder-test.sh HOSTLORDER HOSTNM SIM SH ROOTFS AS SRCDIR NAME FIXTURE
#
# THIS IS THE ONLY TEST IN C9c THAT RUNS A PIPELINE RATHER THAN A PROGRAM, because lorder
# is the only thing in it that is not a program: it is a shell script, and what has to be
# asserted is that the whole chain parses and runs on this machine.  ../../lorder.sh.in
# carries two v7-shell accommodations -- backquotes rather than $( ), ${name-word} rather
# than ${name:-word} -- and neither is checkable by reading the script.  This is where they
# are checked.
#
# HOSTLORDER is the CONFIGURED host script (build/cmd/lorder/lorder.sh), driven with NM
# pointing at the freshly built b6nm -- not an installed b6lorder, which would be a
# different version of the same source and would make this test say nothing.  SH and the
# staged lorder are the disk image's own copies, which is scripts/run-prog-test.sh's rule
# and holds here for the same reason: the bytes this runs are the bytes the kernel will run.
#
# EVERY PROGRAM THE SCRIPT NAMES IS COPIED INTO THE WORKING DIRECTORY, and that is not
# tidiness.  b6sim resolves PATH against the HOST filesystem, so a bare `sed' in the script
# would find the host's own /bin/sed, fail to exec it -- it is not a BESM-6 a.out -- and be
# read back as a shell script, which fails with a syntax error twenty lines in.  The guest
# shell's default path is ":/bin:/usr/bin" and the leading empty entry is the current
# directory, so a copy beside the script is what gets found.  cmd/sh/test/run-sh-test.sh
# brings ./echo along for the same reason and says so.  `rm' is in the list because of the
# trap: the script removes its two scratch files on the way out, and a missing rm turns a
# passing run into exit status 2 at the last moment.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of a
# whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp) -- and
# here that would include PATH, which is exactly the variable that must not leak in.
set -e
hostlorder=$1
hostnm=$2
sim=$3
sh=$4
rootfs=$5
as=$6
srcdir=$7
name=$8
fixture=$9

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

# The script, and every program it names.
cp "$rootfs/usr/bin/lorder" .
cp "$rootfs/usr/bin/nm" .
for p in sed sort join basename rm; do
    cp "$rootfs/bin/$p" .
done

# Assemble the parts.  Relative names, so nothing records a build-machine path -- and
# lorder prints the object names it was given, so an absolute one would land in the diff.
n=1
objs=
for s in "$srcdir/$fixture"*.s; do
    cp "$s" "p$n.s"
    "$as" -o "p$n.o" "p$n.s"
    objs="$objs p$n.o"
    n=$((n + 1))
done

NM="$hostnm" sh "$hostlorder" $objs > "$name.host"
env -i "$sim" "$sh" ./lorder $objs > "$name.native"

diff -u "$name.host" "$name.native"
