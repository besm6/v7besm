#!/bin/sh
# The CLOCK-and-RING oracle for pr(1), under b6sim.  Task C5f.  Invoked by ctest as:
#
#	run-pr-test.sh SIM PROG SRCDIR
#
# WHY THESE ARE NOT b6_progtest CASES.  Two reasons, and the first is the unusual one.
#
#   THE PAGE HEADER CONTAINS A DATE, and it is the file's own mtime -- or, for standard
#   input, the time of day.  Neither is reproducible, so a checked-in .expected can never
#   hold a header line, and every case in ../CMakeLists.txt therefore passes -t.  That would
#   leave pr's whole reason for existing -- the 5-line heading, the 5-line trailer, the page
#   numbering, +n and -h -- asserted nowhere at all.  This script fixes an mtime with
#   `TZ=UTC0 touch -t' and asserts the header exactly.  (TZ matters: under b6sim ftime()
#   answers zone 0, so this libc's ctime() is UTC, while touch reads local time.)
#
#   THE RING OVERRUN NEEDS AN INPUT LARGER THAN THE RING, which is 6,720 bytes.  It is
#   GENERATED here rather than checked in, as ../../sort/test/run-sort-test.sh generates its
#   multi-pass input, and for the same reason: the size has to be chosen against a constant
#   in the program rather than guessed.
#
# WHAT THE RING CASE IS ABOUT.  pr reads ahead: with -N, column k of a page is the text
# N*(length-margin) lines further on than column 1's, so all N cursors live inside one
# 6,720-byte circular buffer at once.  Nothing in v7 checked that the writer had not lapped a
# cursor, and when it had, the affected column silently began PART WAY DOWN the file with no
# diagnostic of any kind -- `pr -l800 -2' over 24 KB starts its first column 840 lines late.
# It takes no exotic input: any two-column listing of a long page over a file bigger than the
# ring does it.  nexbuf() measures the distance now and refuses.
#
# ENV -i, for the reason ../../sh/test/run-sh-test.sh gives: b6sim passes a whitelist of host
# variables through, so an emptied environment is the only one that is the same everywhere.
set -e
sim=$1
prog=$2
srcdir=$3

rm -rf prdir
mkdir prdir
cp "$srcdir/m1.txt" prdir/page.txt
cd prdir

# A fixed mtime, so the header below is a literal rather than a projection.
TZ=UTC0 touch -t 202601020304.05 page.txt

# 1.  The default page: two blank lines, the heading, three blank lines, the body, and blanks
#     to line 66.  -l14 shortens it to 14 so the expectation can be read; the shape is the
#     same, 5 + (14-5) with the body padded out to line 9 and the trailer to line 14.
env -i "$sim" "$prog" -l14 page.txt >out1.txt 2>&1
diff -u - out1.txt <<'EOF'


Jan  2 03:04 2026  page.txt Page 1


A1
A2
A3
A4





EOF

# 2.  -h names the heading instead of the file, AND IT MAY CONTAIN A SPACE -- which is the
#     other thing no b6_progtest case can do, run-prog-test.sh splitting a .args line on
#     whitespace with no quoting (../../README.md §9).
env -i "$sim" "$prog" -l14 -h "My Report" page.txt >out2.txt 2>&1
diff -u - out2.txt <<'EOF'


Jan  2 03:04 2026  My Report Page 1


A1
A2
A3
A4





EOF

# 3.  Page numbering, and +n suppressing everything before page n.  -l12 gives a body of two
#     lines, so four lines of input are two pages -- and -l12 rather than anything smaller
#     because WITHOUT -t a length at or below the 10-line margin is silently reset to 66.
env -i "$sim" "$prog" -l12 page.txt >out3.txt 2>&1
diff -u - out3.txt <<'EOF'


Jan  2 03:04 2026  page.txt Page 1


A1
A2







Jan  2 03:04 2026  page.txt Page 2


A3
A4





EOF

env -i "$sim" "$prog" -l12 +2 page.txt >out4.txt 2>&1
diff -u - out4.txt <<'EOF'


Jan  2 03:04 2026  page.txt Page 2


A3
A4





EOF

# 4.  THE RING.  24,000 bytes -- more than three times the 6,720-byte buffer -- laid out two
#     columns of 800 lines, so the two cursors are 6,400 bytes apart and the writer laps the
#     trailing one.  v7 printed a first column beginning at line 840 and said nothing.
awk 'BEGIN { for (i = 0; i < 3000; i++) printf "L%06d\n", i }' >lap.txt
set +e
env -i "$sim" "$prog" -t -l800 -2 lap.txt >out5.txt 2>err5.txt
set -e
diff -u - err5.txt <<'EOF'
pr: too many columns for the buffer
EOF

# ... and one column short of it is accepted and correct: the same file at -l700 keeps the
# two cursors 5,600 bytes apart, which fits, and the first line really is the first line.
env -i "$sim" "$prog" -t -l700 -2 lap.txt >out6.txt 2>err6.txt
cmp /dev/null err6.txt
head -1 out6.txt | grep -q '^L000000' || {
    echo "cmd_pr_files: -l700 -2 did not start at the first line" >&2
    head -1 out6.txt >&2
    exit 1
}

echo "cmd_pr_files: the page heading is the file's own mtime, -h takes a header with a space"
echo "              in it, +n skips whole pages, and a look-ahead past the ring is refused"
echo "              rather than silently starting a column part way down the file"
