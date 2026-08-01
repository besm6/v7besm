#!/bin/sh
# The FILE oracle for sort(1), under b6sim.  Task C5d.  Invoked by ctest as:
#
#	run-sort-test.sh SIM PROG SRCDIR
#
# WHY THIS IS NOT A b6_progtest CASE.  b6_progtest() diffs standard output and checks an exit
# status and can do no more (../../../scripts/run-prog-test.sh).  Four of sort's promises are
# about FILES and one is about a file that has to stop existing, so none of them shows up on
# standard output at all.  ../../uniq/test/run-uniq-test.sh is the shape this copies.
#
#   1.  -o writes the answer to the named file and NOTHING to standard output, and it
#       truncates rather than appends, so a second run leaves the same bytes.
#
#   2.  THE OUTPUT FILE MAY BE ONE OF THE INPUTS.  sort.1 promises it, and it is the only
#       reason safeoutfil() exists: under -m it stat()s the output against the inputs and
#       sets `unsafeout', which forces the merge through a temp file instead of opening the
#       output for writing while it is still being read.  A port that dropped safeoutfil()
#       would truncate the file before reading it -- and would pass every case that only
#       looks at standard output.
#
#   3.  A MULTI-PASS SORT, which is the whole of the temp-file machinery -- newfile(), the
#       merge over the temp files, and the unlink loop in term().  The input is GENERATED
#       here rather than checked in, and its size is chosen so that ONE PASS IS IMPOSSIBLE
#       BY ARITHMETIC rather than by an estimate of the arena: 4,000 lines of 47 bytes is
#       188,000 bytes of text, and the whole user address space is 28,672 words = 172,032
#       bytes (§6).  No arena can hold it in either world, so this case cannot quietly stop
#       exercising the merge if the program's size changes.  The answer is checked against
#       `LC_ALL=C sort', which for the no-options case really is exactly this program's
#       ordering -- both compare whole lines as unsigned bytes.  This is the ONE place in
#       the suite where a second implementation earns its keep (../CMakeLists.txt says why
#       it does not elsewhere).
#
#   4.  NOTHING IS LEFT BEHIND.  term()'s unlink loop is asserted nowhere else in the tree,
#       and a temp file that survives is invisible to every oracle that reads stdout.
#
# THE ARENA SIZE DIFFERS BETWEEN THE TWO WORLDS, so nothing here may assert a pass count or
# a temp-file count.  b6sim refuses a break at 070000 (`addr >= STACK_BASE') where the kernel
# refuses one ABOVE it (estabur()'s `nt + nd > USTKPAGE * PGSZ'), so the simulator's heap is
# one page smaller and the same input takes a different number of passes there.  What is
# asserted is the answer and the cleanup, both of which are the same in either world.
#
# ENV -i, for the reason ../../sh/test/run-sh-test.sh gives: b6sim passes a whitelist of host
# variables through, so an emptied environment is the only one that is the same everywhere.
set -e
sim=$1
prog=$2
srcdir=$3

rm -rf sortdir
mkdir sortdir
cp "$srcdir/words.txt" sortdir/in
cp "$srcdir/m1.txt" sortdir/f
cp "$srcdir/m2.txt" sortdir/g
cd sortdir

# 1.  sort -o out in -- the answer goes to the file, and nothing goes to standard output.
env -i "$sim" "$prog" -T . -o out in >stdout.txt 2>&1
cmp /dev/null stdout.txt
diff -u - out <<'EOF'
Apple
Banana
apple
apple
banana
cherry
EOF

# ... the named input is untouched, which is what says the two were not crossed.
cmp "$srcdir/words.txt" in

# ... and the output is truncated and not appended to, so a second run leaves the same bytes.
cp out first.txt
env -i "$sim" "$prog" -T . -o out in >stdout2.txt 2>&1
cmp first.txt out

# 2.  The output file IS one of the inputs, under -m: safeoutfil()/unsafeout.
env -i "$sim" "$prog" -T . -m -o f f g >stdout3.txt 2>&1
cmp /dev/null stdout3.txt
diff -u - f <<'EOF'
apple
banana
cherry
damson
EOF

# 3.  A sort that cannot fit in the arena, so it really makes temp files and merges them.
awk 'BEGIN { for (i = 0; i < 4000; i++) printf "%08d-a-line-of-perfectly-ordinary-length\n", (i * 48271) % 1000003 }' > big.in
env -i "$sim" "$prog" -T . big.in -o big.out
LC_ALL=C sort big.in > big.ref
cmp big.ref big.out

# ... and the program agrees with its own answer.
env -i "$sim" "$prog" -T . -c big.out

# 4.  Nothing is left behind: term() unlinked every temp file it made.
leftover=$(ls stm* 2>/dev/null || true)
if [ -n "$leftover" ]; then
    echo "cmd_sort_files: temp files survived: $leftover" >&2
    exit 1
fi

echo "cmd_sort_files: -o reaches the named file, an input may be the output, a multi-pass"
echo "                sort matches the host, and no temp file survives"
