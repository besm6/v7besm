#!/bin/sh
# Run one shell script under the built /bin/sh under b6sim, and diff what comes out
# against the checked-in expectation.  Invoked by ctest as:
#
#	run-sh-test.sh SIM SRCDIR SH NAME
#
# SRCDIR/NAME.sh is the script; anything else named SRCDIR/NAME.*.sh is copied along
# beside it, for the scripts that source a second file.  Optional SRCDIR/NAME.args holds
# the positional parameters, and optional SRCDIR/NAME.status the exit status expected
# (0 if absent) -- the shell reports through its status as well as its output, and a
# test that did not check it would miss `exit N' entirely.
#
# ENV -i IS NOT OPTIONAL.  b6sim hands the guest whichever of a whitelist of host
# variables happen to be set (ENV_WHITELIST in cmd/sim/session.cpp), the shell reads all
# of them into its name tree at startup, and `set' prints the tree.  Cleared, the
# environment is empty on every machine and the output is the shell's own.
set -e
sim=$1
srcdir=$2
sh=$3
name=$4

cp "$srcdir/$name.sh" .
for f in "$srcdir/$name".*.sh; do
    [ -e "$f" ] && cp "$f" .
done

# /bin/echo COMES ALONG, and it is the only binary that does.  b6sim resolves PATH against
# the HOST filesystem, so a bare `echo' in a script would find the host's ELF /bin/echo,
# fail to exec it, and be read back as a shell script; but b6sim does fork and exec a real
# BESM-6 a.out, so a copy named ./echo beside the script works.  It is here because `set'
# -- every other script's oracle -- shows what the name tree holds and cannot show what
# reached ARGV, which is the one thing utf8.sh has to assert.
cp "$(dirname "$sh")/echo" . 2>/dev/null || true

args=$(cat "$srcdir/$name.args" 2>/dev/null || true)
want=$(cat "$srcdir/$name.status" 2>/dev/null || echo 0)

set +e
env -i "$sim" "$sh" "./$name.sh" $args > "$name.out" 2>&1
got=$?
set -e

if [ "$got" != "$want" ]; then
    echo "$name: exit status $got, expected $want" >&2
    cat "$name.out" >&2
    exit 1
fi

diff -u "$srcdir/$name.expected" "$name.out"
