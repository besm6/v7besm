#!/bin/sh
#
# The three programs of task C4e that can see each other's work, in one case.
#
#	run-icheck-cleared.sh SIM B6FSUTIL CLRI ICHECK DCHECK SRCDIR FIXTURE INUM
#
# clri(1M) throws an i-node away.  What is left behind is exactly what the other two exist
# to notice: icheck(1M) finds the file's blocks in no file and on no free list -- `missing'
# -- and dcheck(1M) finds a directory entry naming an inode with no links.  clri.1m's
# DESCRIPTION says this in prose ("any blocks in the affected file will show up as `missing'
# in an icheck of the filesystem"); this is that sentence made executable.
#
# IT LIVES HERE AND NOT IN ../../clri/test BECAUSE OF CONFIGURE ORDER.  cmd/clri and
# cmd/dcheck are added to the top-level CMakeLists.txt before cmd/icheck (they sort earlier),
# so their test directories cannot hang b6prog_icheck on build_tests -- and `make test'
# builds build_tests and nothing else, so a case there could run against a stale icheck.
# This directory is configured after all three.
#
# THE POLARITY IS THE OTHER WAY ROUND FROM EVERY OTHER CASE IN THE TREE, and it is worth
# saying out loud: clri is the only program here whose success is the host's checker
# FAILING.  ../../fsck/test asserts that `b6fsutil -c' passes after the guest has run; here
# it must not, because a cleared inode with a directory entry still pointing at it is a
# broken filesystem and that is precisely what was asked for.
#
set -e

sim="$1"
b6fsutil="$2"
clri="$3"
icheck="$4"
dcheck="$5"
srcdir="$6"
fixture="$7"
inum="$8"

img="cleared.img"
rm -f "$img" cleared.out cleared.after
cp "$fixture" "$img"

# The fixture is sound to start with: a case that measured damage which was already there
# would say nothing about clri.
if ! "$b6fsutil" -c "$img" >/dev/null 2>&1; then
    echo "FAIL: cleared: the fixture is not clean to start with." >&2
    exit 1
fi

# env -i because b6sim forwards a whitelist of host variables.
env -i "$sim" "$clri" "$img" "$inum" >cleared.out 2>&1
env -i "$sim" "$icheck" -m "$img" >>cleared.out 2>&1
env -i "$sim" "$dcheck" "$img" >>cleared.out 2>&1
cat cleared.out

if ! diff -u "$srcdir/cleared.expected" cleared.out; then
    echo "FAIL: cleared: the three programs said something else." >&2
    exit 1
fi

# ... and the host, independently, has to see a broken filesystem now.  See the header.
if "$b6fsutil" -c "$img" >cleared.after 2>&1; then
    cat cleared.after >&2
    echo "FAIL: cleared: clri zeroed an i-node and b6fsutil still calls the image clean." >&2
    exit 1
fi
cat cleared.after

exit 0
