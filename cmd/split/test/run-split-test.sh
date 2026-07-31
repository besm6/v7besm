#!/bin/sh
# The FILE oracle for split(1), under b6sim.  Task C5a.  Invoked by ctest as:
#
#	run-split-test.sh SIM PROG
#
# ../../tee/test/run-tee-test.sh's header has the argument for why a filter that writes FILES
# needs an assertion outside b6_progtest(); this is the second program of the six to need one
# and the first whose output is a whole directory rather than a named file.
#
# It builds its own inputs rather than checking them in, because one of them is 677 lines long
# and exists only to reach a bound.
#
# THREE THINGS ARE ASSERTED AND THE THIRD IS AN UPSTREAM BUG.
#
#   1.  The pieces are the input, cut where asked.  `cat' of them in name order must be the
#       input byte for byte, and each but the last must have exactly n lines.
#   2.  A name operand replaces the `x' prefix, and the pieces land in the working directory.
#   3.  The 677th piece is REFUSED.  The suffix is two letters, `fnumber/26' and `fnumber%26'
#       offset from `a', so v7 ran out of alphabet at 676 and carried on into `{a', `{b', ...
#       -- with no complaint, and with split.1 still promising the names came out in
#       lexicographic order.  What this asserts is that 676 pieces are written, that the name
#       of the last is `xzz', and that the run then stops with a diagnostic and status 1.
set -e
sim=$1
prog=$2

# env -i for run-prog-test.sh's reason: b6sim passes a whitelist of host variables through,
# so an emptied environment is the only one that is the same on every machine.

# ---- 1.  Five lines in twos: xaa, xab, xac.
rm -rf splitdir
mkdir splitdir
cd splitdir
printf 'one\ntwo\nthree\nfour\nfive\n' >in
env -i "$sim" "$prog" -2 in
test -f xaa -a -f xab -a -f xac
test ! -f xad
printf 'one\ntwo\n' >want-aa && cmp want-aa xaa
printf 'three\nfour\n' >want-ab && cmp want-ab xab
printf 'five\n' >want-ac && cmp want-ac xac
cat xaa xab xac >joined && cmp in joined
cd ..

# ---- 2.  A name operand, and the pieces still land in the working directory.
rm -rf namedir
mkdir namedir
cd namedir
printf 'a\nb\nc\n' >in
env -i "$sim" "$prog" -2 in part
test -f partaa -a -f partab
test ! -f xaa
cat partaa partab >joined && cmp in joined
cd ..

# ---- 3.  The 677th piece, which v7 named `x{a' and this refuses.
rm -rf manydir
mkdir manydir
cd manydir
awk 'BEGIN { for (i = 1; i <= 677; i++) print i }' >in
set +e
env -i "$sim" "$prog" -1 in >out 2>&1
status=$?
set -e
if [ "$status" != 1 ]; then
    echo "split -1 over 677 lines: exit status $status, expected 1" >&2
    cat out >&2
    exit 1
fi
diff -u - out <<'EOF'
split: more than 676 output files
EOF
n=$(ls x* | wc -l)
if [ "$n" -ne 676 ]; then
    echo "expected 676 pieces, found $n" >&2
    exit 1
fi
test -f xaa -a -f xzz
cat $(ls x*) >joined
head -676 in >want
cmp want joined
cd ..

echo "cmd_split_pieces: pieces, prefix and the 676-file bound"
