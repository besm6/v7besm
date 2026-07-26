#!/bin/sh
# Run one library test program under b6sim and diff its output against the checked-in
# expectation.  Invoked by ctest as: run-test.sh SIM SRCDIR PROG
#
# The program is run in the current directory (the build tree, where PROG was linked), with
# arguments from SRCDIR/PROG.args if that file exists.  stdout and stderr are captured together
# -- perror writes to fd 2, and anything b6sim itself says should land in the diff rather than
# scroll past.  `set -e' makes a non-zero exit from b6sim fail the test on its own, before the
# diff: a program reports failure by its status as well as its output.
#
# @srcdir@ IN A .args FILE is substituted with SRCDIR, so an argument can name a file in the
# source tree by absolute path -- which a static file cannot spell.  termcapt is what needs it:
# it takes the /etc/termcap to read, absolute (tgetent treats a relative $TERMCAP as an entry
# rather than a file name), and reading THE SAME etc/termcap the disk image is staged from is
# what lets one .expected adjudicate the b6sim run and the run under the booted kernel.
set -e
sim=$1
srcdir=$2
prog=$3
args=$(sed -e "s|@srcdir@|$srcdir|g" "$srcdir/$prog.args" 2>/dev/null || true)
"$sim" "./$prog" $args > "$prog.out" 2>&1
diff -u "$srcdir/$prog.expected" "$prog.out"
