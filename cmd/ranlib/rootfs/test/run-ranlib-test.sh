#!/bin/sh
# Two ranlibs built from one source must build the same __.SYMDEF.  Task C9d,
# ../../TODO.md.  Invoked by ctest as:
#
#	run-ranlib-test.sh HOSTRANLIB HOSTAR SIM PROG PROGAR AS LIBCDIR SRCDIR NAME KIND
#
# HOSTRANLIB and HOSTAR are the freshly built host tools (build/cmd/{ranlib,ar}/b6* --
# NOT installed ones, which would be different versions of the same source and would
# make this test say nothing).  PROG and PROGAR are the native ranlib and ar AS STAGED
# FOR THE DISK IMAGE: the bytes this runs are the bytes the kernel will run.
#
# BOTH SIDES START FROM ONE ARCHIVE, built by the host b6ar and copied twice, so the
# input is identical by construction and only the indexing is under test.  The one
# exception is `chain', where each side builds the archive with its OWN ar as well --
# that case is the closing claim of C9d and deliberately trusts nothing.  Each side
# works in a directory of its own, host/ and native/: ranlib writes its scratch
# __.SYMDEF into the CURRENT DIRECTORY, so the two runs cannot share one.
#
# ONE WORD OF THE RESULT IS NOT DETERMINISTIC AND IS MASKED.  fixdate() (../ranlib.c)
# writes time(NULL) into the __.SYMDEF member header, so two runs a second apart differ
# there and nowhere else: every other member's ar_date is copied through from the input,
# and the index member's uid, gid and mode come from the same host stat(2), which b6sim
# passes through unchanged.  The header is fixed-size -- the archive opens with a 6-byte
# ARMAG, then 1 length byte + the 9 bytes of "__.SYMDEF" + 2 bytes of word padding --
# so ar_date is archive BYTES 18..23, and the two ranges either side of it are compared
# separately.  Confirmed against a real archive:
#
#	od -A d -t x1 -N 24 build/lib/libc/libc.a
#	0000000  00 00 00 00 ff 65  09 5f 5f 2e 53 59 4d 44 45 46  00 00  <ar_date>
#
# THE `symdef' CASE NEEDS NO MASK AT ALL, which is why it is here: it extracts the
# __.SYMDEF MEMBER and compares the body, and the body carries no timestamp.  Same
# symbols, same order, same ran_off values -- the strongest assertion in this suite,
# since __.SYMDEF's order is what makes b6ld's single scan of a library work.
#
# ENV -i for the b6sim runs, as run-prog-test.sh does: b6sim hands the guest whichever of
# a whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostranlib=$1
hostar=$2
sim=$3
prog=$4
progar=$5
as=$6
libcdir=$7
srcdir=$8
name=$9
shift 9
kind=$1

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

run_host()
{
    "$hostranlib" "$@"
}

run_native()
{
    env -i "$sim" "$prog" "$@"
}

ar_host()
{
    "$hostar" "$@"
}

ar_native()
{
    env -i "$sim" "$progar" "$@"
}

# The inputs.  `chain' hands each side the C library's objects and has it build the
# archive itself; `libc' hands both sides the finished libc.a; everything else uses the
# four fixture members cmd/ar/rootfs/test assembles, so that the two suites cannot drift
# apart on what a member looks like.
fixtures=$srcdir/../../../ar/rootfs/test
case $name in
chain)
    for d in host native; do
        mkdir $d
        cp -p "$libcdir"/*.o $d/
    done
    ;;
libc)
    for d in host native; do
        mkdir $d
        cp -p "$libcdir/libc.a" $d/X.a
    done
    ;;
*)
    n=1
    for s in "$fixtures"/agree*.s; do
        "$as" -o "p$n.o" "$s"
        n=$((n + 1))
    done
    "$hostar" cr X.a p1.o p2.o p3.o p4.o
    for d in host native; do
        mkdir $d
        cp -p X.a $d/X.a
    done
    ;;
esac

scenario()
{
    run=$1
    runar=$2
    case $name in
    index)  $run X.a ;;                        # no __.SYMDEF yet: the `new' branch
    rerun)  $run X.a; $run X.a ;;              # ...and then the replace-in-place one
    touch)  $run X.a; $run -t X.a ;;           # -t: fixdate() and nothing else
    symdef) $run X.a ;;                        # compared through the member, unmasked
    multi)  cp -p X.a Y.a; $run X.a Y.a ;;     # two archives: ar_run() twice in one run
    libc)   $run X.a ;;                        # 222 symbols, the real corpus
    chain)  $runar cr X.a *.o; $run X.a ;;     # the machine builds its own libc.a
    *)      echo "run-ranlib-test.sh: no scenario for $name" >&2; exit 2 ;;
    esac
}

(cd host && scenario run_host ar_host > out 2>&1)
(cd native && scenario run_native ar_native > out 2>&1)

# Compare two archives everywhere except the one word fixdate() owns.
masked_cmp()
{
    cmp -s -n 18 "$1" "$2" && cmp -s -i 24 "$1" "$2"
}

status=0
cmp -s host/out native/out || { echo "output differs: $name"; status=1; }

case $kind in
masked)
    masked_cmp host/X.a native/X.a ||
        { echo "indexed archives differ: $name"; status=1; }
    if [ "$name" = multi ]; then
        masked_cmp host/Y.a native/Y.a ||
            { echo "second indexed archive differs: $name"; status=1; }
    fi
    ;;
symdef)
    # The index MEMBER, byte for byte and with nothing masked.
    "$hostar" p host/X.a __.SYMDEF   > host.symdef
    "$hostar" p native/X.a __.SYMDEF > native.symdef
    cmp -s host.symdef native.symdef ||
        { echo "__.SYMDEF bodies differ: $name"; status=1; }
    masked_cmp host/X.a native/X.a ||
        { echo "indexed archives differ: $name"; status=1; }
    ;;
*)  echo "run-ranlib-test.sh: unknown kind $kind" >&2; exit 2 ;;
esac

[ $status = 0 ] && exit 0

# Differ: say what is in the two archives before failing on the bytes.  Only
# read-only tools here -- `b6ranlib -d' would rebuild the very index under inspection.
diff -u host/out native/out || true
TZ=UTC0 "$hostar" tv host/X.a   > host.tv   2>&1 || true
TZ=UTC0 "$hostar" tv native/X.a > native.tv 2>&1 || true
diff -u host.tv native.tv || true
cmp -l host/X.a native/X.a | head -20 || true
exit 1
