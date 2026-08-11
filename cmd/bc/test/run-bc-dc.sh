#!/bin/sh
# Run one bc(1) program through the whole pipeline and check the NUMBERS it produces:
# bc -c under b6sim, then the dc source that comes out through /bin/dc under b6sim.
# Invoked by ctest as:
#
#	run-bc-dc.sh SIM SRCDIR ROOTFS NAME
#
# SRCDIR/NAME.in is the bc program, SRCDIR/NAME.expected what dc must print, and the
# optional SRCDIR/NAME.args names extra input files for bc with @rootfs@ and @srcdir@
# substituted -- that is how a case reaches the math library.  NAME.status is dc's
# expected exit status, 0 when absent; bc's own must always be 0.
#
# TWO PROCESSES AND NOT A PIPE, because the pipe is the one thing that cannot be tested
# here: bc's default path forks and execs /bin/dc, which b6sim resolves against the HOST
# filesystem.  Feeding the source across by hand runs the same two programs over the same
# bytes and leaves only the fork out.  CMakeLists.txt beside this file says the rest.
#
# ENV -i IS NOT OPTIONAL: b6sim passes a whitelist of host variables through to the guest
# (ENV_WHITELIST in cmd/sim/session.cpp), and cleared, the environment is the same on
# every machine.
set -e
sim=$1
srcdir=$2
rootfs=$3
name=$4

args=$(sed -e "s|@rootfs@|$rootfs|g" -e "s|@srcdir@|$srcdir|g" \
        "$srcdir/$name.args" 2>/dev/null || true)
want=$(cat "$srcdir/$name.status" 2>/dev/null || echo 0)

set +e
env -i "$sim" "$rootfs/bin/bc" -c $args < "$srcdir/$name.in" > "$name.dc" 2>&1
got=$?
set -e
if [ "$got" != 0 ]; then
    echo "$name: bc exit status $got, expected 0" >&2
    cat "$name.dc" >&2
    exit 1
fi

set +e
env -i "$sim" "$rootfs/bin/dc" - < "$name.dc" > "$name.out" 2>&1
got=$?
set -e

if [ "$got" != "$want" ]; then
    echo "$name: dc exit status $got, expected $want" >&2
    cat "$name.dc" "$name.out" >&2
    exit 1
fi

diff -u "$srcdir/$name.expected" "$name.out"
