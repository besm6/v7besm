#!/bin/sh
# The FILE oracle for uniq(1), under b6sim.  Task C5b.  Invoked by ctest as:
#
#	run-uniq-test.sh SIM PROG SRCDIR
#
# WHY THIS IS NOT A b6_progtest CASE.  b6_progtest() diffs standard output and checks an exit
# status and can do no more (../../../scripts/run-prog-test.sh).  uniq is a filter and nine of
# its ten cases are exactly that -- but it also takes TWO file arguments, `uniq input output',
# and in that shape its answer is a file that standard output says nothing about.
# ../../tee/test/run-tee-test.sh is the shape this copies.
#
# THE ARGUMENT ORDER IS THE PART A CARELESS TEST WOULD MISS, and it is easy to get backwards
# because uniq reaches the two files through freopen() on stdin and stdout rather than through
# open(2): argv[1] is redirected onto DESCRIPTOR 0 and argv[2] onto DESCRIPTOR 1.  A port that
# swapped them would still produce a plausible-looking run -- it would read the output file,
# find it empty, and write nothing -- and every stdout case here would go on passing.  So the
# assertion is that the named output holds the answer AND that the named input is unchanged.
#
# THE OUTPUT FILE IS CREATED, NOT APPENDED TO.  freopen(..., "w", stdout) truncates, so a
# second run over a file that already has content must leave the same bytes and not twice
# them -- the mirror of the property tee's -a case pins from the other side.
set -e
sim=$1
prog=$2
srcdir=$3

rm -rf uniqdir
mkdir uniqdir
cp "$srcdir/dup.txt" uniqdir/in
cd uniqdir

# env -i for run-prog-test.sh's reason: b6sim passes a whitelist of host variables through,
# so an emptied environment is the only one that is the same on every machine.

# 1.  uniq input output -- the answer goes to the file, and nothing goes to standard output.
env -i "$sim" "$prog" in out >stdout.txt 2>&1
cmp /dev/null stdout.txt
diff -u - out <<'EOF'
alpha
beta
gamma
delta
EOF

# 2.  The named input is untouched, which is what says the two descriptors did not get
#     crossed: a uniq that read argv[2] and wrote argv[1] would leave `in' holding the answer.
cmp "$srcdir/dup.txt" in

# 3.  The output is truncated and not appended to, so a second run leaves the same bytes.
cp out first.txt
env -i "$sim" "$prog" in out >stdout2.txt 2>&1
cmp first.txt out

# 4.  ... and -c through the same pair, so the flag and the file arguments are shown to
#     coexist -- the option loop consumes argv as it goes and stops at the first non-flag.
env -i "$sim" "$prog" -c in count.txt >stdout3.txt 2>&1
cmp /dev/null stdout3.txt
diff -u - count.txt <<'EOF'
   2 alpha
   1 beta
   3 gamma
   1 delta
EOF

echo "cmd_uniq_files: the answer reaches the named output, the named input is unchanged"
