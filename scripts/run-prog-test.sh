#!/bin/sh
# Run one native BESM-6 program under b6sim with a fixed argument list, and diff what comes
# out against the checked-in expectation.  Invoked by ctest as:
#
#	run-prog-test.sh SIM SRCDIR PROG NAME
#
# where PROG is the program as it was STAGED FOR THE DISK IMAGE (build/rootfs/bin/x).  That
# is deliberate and is lib/test's property too: the bytes this harness runs are the bytes
# the kernel will run, not a second link of the same sources.
#
# Optional SRCDIR/NAME.args holds the arguments (one line, split on whitespace by the shell
# under `set -f', with @srcdir@ substituted as lib/test/run-test.sh does); optional
# SRCDIR/NAME.status holds
# the exit status expected, 0 if absent -- a program that reports through its status alone
# would otherwise be untested.  SRCDIR/NAME.expected is the output, stdout and stderr
# together.
#
# STANDARD INPUT comes from optional SRCDIR/NAME.in, and from /dev/null when there is no such
# file.  Until task C3 there was none of either: `env -i' passed the ctest harness's own
# stdin through, so nothing could feed a program and nothing was reading anyway.  ed(1) is
# the first program here that takes its whole COMMAND LANGUAGE on stdin, which is what made
# the addition worth making -- cmd/README.md SS9 had named it as the obvious one -- and task
# C5's filters will all want it.  A case's file is fed verbatim: no @srcdir@ substitution,
# since an input stream is data and not a command line.
#
# ENV -i, for the reason cmd/sh/test/run-sh-test.sh gives: b6sim hands the guest whichever
# of a whitelist of host variables happen to be set (ENV_WHITELIST in cmd/sim/session.cpp),
# so an emptied environment is the only one that is the same on every machine.
#
# WHAT MAY BE TESTED HERE.  b6sim services every system call on the HOST, so a case may
# assert only what is reproducible there: argument handling, diagnostics, exit status.
# Anything that touches global machine state belongs under the booted kernel instead --
# b6sim's stime() is a no-op that reports success, and its kill() goes straight to the
# build machine's, so no case here may send a signal to a real pid.  See kernel/test/utils.
#
# ARGUMENTS ARE SPLIT BUT NOT GLOBBED.  `set -f' is on because task C5c's arguments are
# REGULAR EXPRESSIONS: `[bg]', `g*amma' and `^[^abg]' are all pathname patterns as well, and
# without it a file happening to sit in the test's working directory would silently replace
# the pattern with its name.  No .args file has ever wanted globbing.  What is still true is
# that the line is split on whitespace, so a pattern CONTAINING A SPACE cannot be a case here
# -- there is no quoting -- and belongs under the booted kernel, where the shell quotes it.
set -ef
sim=$1
srcdir=$2
prog=$3
name=$4

args=$(sed -e "s|@srcdir@|$srcdir|g" "$srcdir/$name.args" 2>/dev/null || true)
want=$(cat "$srcdir/$name.status" 2>/dev/null || echo 0)

stdin=/dev/null
if [ -f "$srcdir/$name.in" ]; then
    stdin="$srcdir/$name.in"
fi

set +e
env -i "$sim" "$prog" $args < "$stdin" > "$name.out" 2>&1
got=$?
set -e

if [ "$got" != "$want" ]; then
    echo "$name: exit status $got, expected $want" >&2
    cat "$name.out" >&2
    exit 1
fi

diff -u "$srcdir/$name.expected" "$name.out"
