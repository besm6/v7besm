#!/bin/sh
# Two ccs built from one source must drive the chain the same way, byte for byte.
# Task C9e, ../../TODO.md.  Invoked by ctest as:
#
#	run-cc-test.sh HOSTCC SIM PROG HOSTCPP HOSTAS HOSTLD ROOTFS LIBCDIR RTDIR SRCDIR NAME KIND
#
# HOSTCC is the freshly built host tool (build/cmd/cc/b6cc -- NOT an installed one,
# which would be a different version of the same source and would make this test say
# nothing).  PROG is the native cc AS STAGED FOR THE DISK IMAGE,
# build/rootfs/usr/bin/cc, which is scripts/run-prog-test.sh's rule and holds here for
# the same reason: the bytes this runs are the bytes the kernel will run.
#
# WHAT IS UNDER TEST IS THE COMMAND LINES, NOT THE BYTES.  cpp, as and ld have agreed
# with their native selves since tasks C9a and C9b, so an object that comes out the
# same says the driver named the same input, the same output and the same flags at
# every stage.  That is the whole job of a driver and it is the whole of this suite.
#
# BOTH SIDES ARE PINNED WITH THE B6* OVERRIDES, and on the native side that is not a
# convenience but a correctness requirement.  b6sim resolves an exec on the HOST
# filesystem, and /usr/bin/cc's search path is /usr/bin -- so an unpinned run would
# hand the fixture to the build machine's own assembler.  cmd/sim/session.cpp carries
# the six names on ENV_WHITELIST for exactly this, and cmd/sh/test/run-sh-test.sh meets
# the same hazard with PATH.  The host side is pinned for the ordinary reason: the
# installed b6cpp/b6as/b6ld are a different build of the same sources.
#
# NOTHING IS MASKED, and it is worth saying because cmd/ranlib's suite next door has to
# mask six bytes: this driver stamps nothing.  Its own output is whatever the stages
# wrote, the stages write no timestamp, and the only place a temporary's name would
# reach the output is under -v, which no case here runs.
#
# EACH SIDE RUNS IN A DIRECTORY OF ITS OWN, host/ and native/, inside a working
# directory named after the case: ctest runs the cases in parallel, and a `cc' with no
# -o writes its .o beside the source.  Every argument below is a RELATIVE name -- an
# absolute build-machine path would land in a diagnostic and could never be identical
# on the two sides.
#
# -nostdlib ON EVERY LINK, and crt0.o, -L and the two -l's named by hand instead.  The
# implicit search is the one thing here that CANNOT be compared: the native driver looks
# in /lib, which under b6sim is the BUILD MACHINE's /lib, while the host driver looks in
# ~/.local/share/besm6/lib and finds a real crt0.o -- so the two sides would differ over
# a directory rather than over the driver.  Naming the same files on both command lines
# is what makes the comparison mean something, and it still exercises the whole of the
# link: the crt0 argument, the object order, the -L pass-through and the -lc -lruntime
# that closes the line.  What the machine does with the real /lib the image now carries
# is a different question, and not one b6sim can be asked.
#
# ENV -i for the b6sim run, as run-prog-test.sh does: b6sim hands the guest whichever of
# a whitelist of host variables happen to be set (ENV_WHITELIST, cmd/sim/session.cpp).
set -e
hostcc=$1
sim=$2
prog=$3
hostcpp=$4
hostas=$5
hostld=$6
rootfs=$7
libcdir=$8
rtdir=$9
shift 9
srcdir=$1
name=$2
kind=$3

rm -rf "$name.dir"
mkdir "$name.dir"
cd "$name.dir"

# The two implementations, each spelled once.  `$run' below expands to one of these
# names and the shell looks the function up, so a scenario is written once and run
# twice.
run_host()
{
    B6CPP="$hostcpp" B6AS="$hostas" B6LD="$hostld" "$hostcc" "$@"
}

run_native()
{
    env -i B6CPP="$rootfs/usr/bin/cpp" B6AS="$rootfs/usr/bin/as" \
           B6LD="$rootfs/usr/bin/ld" "$sim" "$prog" "$@"
}

# The fixtures, one copy per side.  cp -p rather than cp for the habit cmd/ar's
# harness needs and this one does not: nothing here reads an mtime, but a fixture
# that did would be a silent difference.
for d in host native; do
    mkdir $d $d/inc
    cp -p "$srcdir/agree.s" "$srcdir/preasm.S" $d/
    cp -p "$srcdir/inc/extra.h" $d/inc/
done

# One scenario per case, run once per implementation.  The comparison list is `kind',
# a space-separated set of file names -- standard output is compared in every case
# anyway, so kind only adds to it.
scenario()
{
    run=$1
    case $name in
    assemble) $run -c agree.s -o X.o ;;
    preasm)   $run -c preasm.S -o X.o ;;
    cpp)      $run -E preasm.S -o X.i ;;
    defines)  $run -E -D EXTRA=7 -I inc preasm.S -o X.i ;;
    link)     $run -nostdlib -o X "$libcdir/crt0.o" agree.s \
                   -L "$libcdir" -L "$rtdir" -lc -lruntime ;;
    multi)    $run -c agree.s
              $run -c preasm.S
              $run -nostdlib -o X "$libcdir/crt0.o" agree.o \
                   -L "$libcdir" -L "$rtdir" -lc -lruntime ;;
    *)        echo "run-cc-test.sh: no scenario for $name" >&2; exit 2 ;;
    esac
}

# NOT under `set -e': a scenario that fails must fail on BOTH sides and in the same way,
# and aborting here would leave the second side unrun and the diff below with nothing to
# show.  The statuses are compared as well as the bytes -- the driver reports through its
# exit status too, and two runs that print alike and exit differently are not agreement.
set +e
(cd host && scenario run_host > out 2>&1)
hoststatus=$?
(cd native && scenario run_native > out 2>&1)
nativestatus=$?
set -e

status=0
if [ "$hoststatus" != "$nativestatus" ]; then
    echo "exit status differs: $name (host $hoststatus, native $nativestatus)"
    status=1
fi
cmp -s host/out native/out || { echo "output differs: $name"; status=1; }

for f in $kind; do
    cmp -s "host/$f" "native/$f" || { echo "$f differs: $name"; status=1; }
done

[ $status = 0 ] && exit 0

# Differ: say what is in the two files before failing on the bytes, as
# run-ar-test.sh does -- a segment size and a symbol a reader can follow, rather than
# an offset nobody can.
diff -u host/out native/out || true
for f in $kind; do
    cmp -s "host/$f" "native/$f" && continue
    case $f in
    *.i) diff -u "host/$f" "native/$f" || true ;;
    *)   cmp -l "host/$f" "native/$f" | head -20 || true ;;
    esac
done
exit 1
