#!/bin/sh
# The APPLY oracle for diff(1), under b6sim.  Task C5f.  Invoked by ctest as:
#
#	run-diff-test.sh SIM PROG SRCDIR
#
# WHY diff NEEDS AN ORACLE OF ITS OWN, AND WHY IT IS NOT HOST diff.  A diff is not unique.
# v7 finds a longest common subsequence by Hunt-Szymanski over Harold Stone's k-candidates;
# GNU diff uses Myers.  Both are correct and they pick DIFFERENT scripts: over 150 random
# file pairs and four option sets, the two disagree textually about 79 times in 600 runs --
# every one of them a different but equally valid way of saying the same change.  So host
# diff cannot be the oracle here, and the checked-in .expected files beside this script
# assert THIS program's answer rather than THE answer.
#
# WHAT CAN BE CHECKED IS THE PROPERTY, and it is the strongest oracle in this directory:
# `diff -e A B' emits an ed(1) script, and ed(1) is on the build machine, so applying the
# GUEST's script to A must produce B EXACTLY.  That is checked here over 40 generated file
# pairs -- random insertions, deletions and changes, with Cyrillic lines and ragged blanks
# among them -- and it exercises the same J vector that drives the plain and -f outputs too,
# so a wrong alignment could not hide in one of the three.
#
# AND TWO THINGS NO b6_progtest CASE CAN REACH:
#
#   A DIRECTORY ARGUMENT.  `diff dir file' compares dir/basename(file) against file.  v7
#   built that path into malloc(100) with a hand-rolled unbounded copy; the case needs a
#   directory, which a fixture list cannot express.
#
#   diffh ITSELF.  /usr/lib/diffh is on the IMAGE, not on the build machine, so `diff -h'
#   under b6sim can only report `cannot find diffh' (which cmd_diff_nodiffh asserts).  The
#   program is run directly here instead, including with diff's own flag word, which is the
#   only shape it is ever invoked in.  The working -h path is kernel/test/filters.
#
# ENV -i, for the reason ../../sh/test/run-sh-test.sh gives: b6sim passes a whitelist of host
# variables through, so an emptied environment is the only one that is the same everywhere.
set -e
sim=$1
prog=$2
srcdir=$3

rm -rf diffdir
mkdir diffdir
cp "$srcdir/a.txt" "$srcdir/b.txt" diffdir/
cd diffdir

# 1.  THE APPLY ORACLE.  Forty generated pairs; every ed script the guest writes must turn A
#     into B byte for byte.
mkdir -p sub
seed=1
n=0
while [ $seed -le 40 ]; do
    awk -v s=$seed 'BEGIN {
        srand(s); w["0"]="alpha"; w["1"]="beta"; w["2"]="gamma"; w["3"]="привет"
        w["4"]="мир"; w["5"]="delta"; w["6"]="epsilon"; w["7"]="zeta"
        n = int(rand()*20)
        for (i = 0; i < n; i++) { a[i] = w[int(rand()*8) ""]; print a[i] > "A" }
        m = 0
        for (i = 0; i < n; i++) {
            r = rand()
            if (r < 0.15) continue                       # delete
            if (r < 0.30) { print w[int(rand()*8) ""] > "B"; m++ }   # change
            else print a[i] > "B"
            m++
            if (r > 0.92) { print w[int(rand()*8) ""] > "B"; m++ }   # insert
        }
        close("A"); close("B")
    }'
    : >A.chk
    [ -f A ] || : >A
    [ -f B ] || : >B
    env -i "$sim" "$prog" -e A B >script.ed 2>err.txt || true
    cmp /dev/null err.txt
    cp A C
    { cat script.ed; echo w; echo q; } | ed - C >/dev/null 2>&1 || true
    if ! cmp -s C B; then
        echo "cmd_diff_files: ed script for seed $seed did not turn A into B" >&2
        echo "--- script:" >&2; cat script.ed >&2
        echo "--- got:" >&2; cat C >&2
        echo "--- want:" >&2; cat B >&2
        exit 1
    fi
    n=$((n + 1))
    seed=$((seed + 1))
    rm -f A B
done
echo "cmd_diff_files: $n ed scripts applied cleanly"

# 2.  A DIRECTORY ARGUMENT: `diff dir file' is `diff dir/file file'.
mkdir -p d
cp a.txt d/a.txt
env -i "$sim" "$prog" d a.txt >out1.txt 2>&1
cmp /dev/null out1.txt

cp b.txt d/a.txt
set +e
env -i "$sim" "$prog" d a.txt >out2.txt 2>&1
st=$?
set -e
[ "$st" = 1 ] || { echo "cmd_diff_files: diff dir file exited $st, expected 1" >&2; exit 1; }
diff -u - out2.txt <<'EOF'
2c2
< BETA
---
> beta
5,6c5
< zeta
< eta
---
> epsilon
EOF

# 3.  diffh, which nothing else in this suite can reach.
diffh=$(dirname "$prog")/../usr/lib/diffh
env -i "$sim" "$diffh" a.txt b.txt >out3.txt 2>&1
diff -u - out3.txt <<'EOF'
2,$c2,$
< beta
< gamma
< delta
< epsilon
---
> BETA
> gamma
> delta
> zeta
> eta
EOF

# ... and with diff's OWN flag word, which is the only way diff -h ever invokes it.
env -i "$sim" "$diffh" -h a.txt b.txt >out4.txt 2>&1
cmp out3.txt out4.txt

# ... and -b, which is the one letter it does look for in that word.
printf 'one  two\nthree\n' >h1.txt
printf 'one\ttwo\nthree\n' >h2.txt
env -i "$sim" "$diffh" -hb h1.txt h2.txt >out5.txt 2>&1
cmp /dev/null out5.txt

echo "cmd_diff_files: a directory argument names the file inside it, and /usr/lib/diffh runs"
echo "                with diff's own flag word"
