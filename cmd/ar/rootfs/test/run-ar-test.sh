#!/bin/sh
# Two ars built from one source must build the same archive, byte for byte.
# Task C9d, ../../TODO.md.  Invoked by ctest as:
#
#	run-ar-test.sh HOSTAR SIM PROG AS LIBCDIR SRCDIR NAME KIND
#
# HOSTAR is the freshly built host tool (build/cmd/ar/b6ar -- NOT an installed one,
# which would be a different version of the same source and would make this test say
# nothing).  PROG is the native ar AS STAGED FOR THE DISK IMAGE,
# build/rootfs/usr/bin/ar, which is scripts/run-prog-test.sh's rule and holds here for
# the same reason: the bytes this runs are the bytes the kernel will run.
#
# NAME is the ctest case and selects the scenario below; KIND says what is compared --
# `archive' (X.a), `stdout' (what the run printed) or `extract' (the member files the
# run wrote into its own directory).  Standard output is compared in EVERY case anyway;
# KIND only adds to it.
#
# EACH SIDE RUNS IN A DIRECTORY OF ITS OWN, host/ and native/, inside a working
# directory named after the case -- ctest runs the cases in parallel, and `ar x' writes
# member files into the current directory while `ar' under -l writes its temporaries
# there.  The inputs are copied in with `cp -p', which is load-bearing and not tidiness:
# a member's ar_date is the input file's st_mtime, so the two sides must see the very
# same timestamps or every header would differ.
#
# THE INPUTS ARE ASSEMBLED HERE, by the host b6as, rather than checked in as .o files:
# a golden object would freeze the header layout at whatever it was the day it was
# generated, and this suite is the one that has to notice when it moves.  The `libc'
# case is different and is the point of the exercise -- it archives the real
# build/lib/libc/*.o, the C library this very program was linked against.
#
# WHY A LIVE COMPARISON AND NOT A CHECKED-IN ARCHIVE.  The property is agreement
# between two builds of ONE source, and a golden file cannot express it: the day
# someone changes a header field, this REQUIRES the change to land identically on both
# targets, where a golden file merely needs regenerating -- and whoever regenerates it
# from the host tool has quietly stopped testing the delta.  The b6_progtest cases
# beside this one are the other half: they pin the diagnostics, which two ars wrong in
# the same way would not.
#
# TZ=UTC0 ON THE HOST SIDE, and it is load-bearing too.  `ar tv' renders ar_date with
# ctime(3), and under b6sim this libc is told zone 0 (ftime, cmd/sim/syscall.cpp), so
# the native side prints UTC while an unconstrained host side would print the build
# machine's local time.  cmd/pr/test/run-pr-test.sh has the same line for the same
# reason.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of
# a whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostar=$1
sim=$2
prog=$3
as=$4
libcdir=$5
srcdir=$6
name=$7
kind=$8

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

# The two implementations, each spelled once.  `$run' below expands to one of these
# names and the shell looks the function up, so a scenario is written once and run
# twice.
run_host()
{
    TZ=UTC0 "$hostar" "$@"
}

run_native()
{
    env -i "$sim" "$prog" "$@"
}

# Assemble the four fixture members once, then give each side its own copies.
if [ "$name" = libc ]; then
    for d in host native; do
        mkdir $d
        cp -p "$libcdir"/*.o $d/
    done
else
    n=1
    for s in "$srcdir"/agree*.s; do
        "$as" -o "p$n.o" "$s"
        n=$((n + 1))
    done
    for d in host native; do
        mkdir $d
        cp -p p1.o p2.o p3.o p4.o $d/
    done
fi

# One scenario per case, run once per implementation.  Each is written in terms of
# relative names only: an absolute build-machine path would land in `ar tv' output and
# in any diagnostic, and could never be byte-identical.
scenario()
{
    run=$1
    case $name in
    create)  $run cr X.a p1.o p2.o p3.o ;;
    replace) $run cr X.a p1.o p2.o p3.o
             cp -p p4.o p2.o
             $run r X.a p2.o ;;
    update)  $run cr X.a p1.o p2.o p3.o
             $run ru X.a p2.o ;;          # not newer than the member: skipped
    delete)  $run cr X.a p1.o p2.o p3.o
             $run d X.a p2.o ;;
    before)  $run cr X.a p1.o p2.o p3.o
             $run rb p2.o X.a p4.o ;;
    move)    $run cr X.a p1.o p2.o p3.o p4.o
             $run ma p1.o X.a p4.o ;;
    quick)   $run cr X.a p1.o p2.o
             $run q X.a p3.o p4.o ;;
    local)   $run crl X.a p1.o p2.o p3.o ;;
    toc)     $run cr X.a p1.o p2.o p3.o p4.o
             $run t X.a ;;
    tocv)    $run cr X.a p1.o p2.o p3.o p4.o
             $run tv X.a ;;
    print)   $run cr X.a p1.o p3.o
             $run p X.a p3.o ;;
    extract) $run cr X.a p1.o p2.o p3.o p4.o
             rm -f p1.o p2.o p3.o p4.o
             $run x X.a p2.o p4.o ;;
    libc)    $run cr X.a *.o ;;
    *)       echo "run-ar-test.sh: no scenario for $name" >&2; exit 2 ;;
    esac
}

(cd host && scenario run_host > out 2>&1)
(cd native && scenario run_native > out 2>&1)

status=0
cmp -s host/out native/out || { echo "output differs: $name"; status=1; }

case $kind in
archive) cmp -s host/X.a native/X.a || { echo "archives differ: $name"; status=1; } ;;
extract) for f in p2.o p4.o; do
             cmp -s "host/$f" "native/$f" ||
                 { echo "extracted $f differs: $name"; status=1; }
         done ;;
stdout)  ;;                              # the out comparison above is the whole of it
*)       echo "run-ar-test.sh: unknown kind $kind" >&2; exit 2 ;;
esac

[ $status = 0 ] && exit 0

# Differ: say what is in the two archives before failing on the bytes, as
# run-strip-test.sh does -- a member name and a length a reader can follow, rather
# than an offset nobody can.
diff -u host/out native/out || true
if [ -f host/X.a ] && [ -f native/X.a ]; then
    TZ=UTC0 "$hostar" tv host/X.a   > host.tv   2>&1 || true
    TZ=UTC0 "$hostar" tv native/X.a > native.tv 2>&1 || true
    diff -u host.tv native.tv || true
    cmp -l host/X.a native/X.a | head -20 || true
fi
exit 1
