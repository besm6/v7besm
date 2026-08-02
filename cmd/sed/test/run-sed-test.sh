#!/bin/sh
# The FILE oracle for sed(1), under b6sim.  Task C5e.  Invoked by ctest as:
#
#	run-sed-test.sh SIM PROG SRCDIR
#
# WHY THIS IS NOT A b6_progtest CASE.  b6_progtest() diffs standard output and checks an
# exit status and can do no more (../../../scripts/run-prog-test.sh).  Seventy-nine of
# sed's cases are exactly that -- but `w wfile', `s///w wfile' and the whole of what -n
# turns sed into produce a FILE, and standard output says nothing about it.
# ../../uniq/test/run-uniq-test.sh is the shape this copies.
#
# FOUR THINGS ONLY A FILE ORACLE CAN SAY, and the first is the one a careless test misses:
#
#   1. A wfile IS CREATED BEFORE PROCESSING BEGINS -- sed.1 says so in as many words, and
#      it is a property of the COMPILER and not of the editing: fcomp() does the fopen(,
#      "w") while it is reading the script, so a wfile is truncated even by a run whose
#      script then fails, and even by a run that reads no input at all.  Nothing on
#      standard output can distinguish that from a file written at the end.
#
#   2. TWO w COMMANDS NAMING THE SAME FILE SHARE ONE DESCRIPTOR.  fcomp() scans the names
#      it has already opened and reuses the FILE * rather than opening a second one; two
#      independent streams onto one file would interleave by buffer rather than by line,
#      and the damage would be invisible until a buffer happened to fill.
#
#   3. s///w WRITES ONLY WHEN THE SUBSTITUTION HAPPENED.  The flag is on the `s' command,
#      not on the line, so a line the expression did not match must leave no trace.
#
#   4. AND THE PATTERN SPACE REACHES THE FILE AS IT IS *AFTER* THE SUBSTITUTION.  v7 writes
#      linebuf, which dosub() has already copied genbuf back into; a port that wrote the
#      matched text instead would still produce a plausible file of the right length.
set -e
sim=$1
prog=$2
srcdir=$3

rm -rf seddir
mkdir seddir
cp "$srcdir/nums.txt" "$srcdir/writef.sed" "$srcdir/subw.sed" seddir/
cd seddir

# env -i for run-prog-test.sh's reason: b6sim passes a whitelist of host variables
# through, so an emptied environment is the only one that is the same on every machine.

# 1.  w wfile -- the selected lines reach the named file and NOT standard output.
env -i "$sim" "$prog" -n -f writef.sed nums.txt >stdout.txt 2>&1
cmp /dev/null stdout.txt
diff -u - picked.txt <<'EOF'
line1
line3
EOF

# 2.  s///w -- only the line the substitution touched, and in its substituted form.
env -i "$sim" "$prog" -n -f subw.sed nums.txt >stdout2.txt 2>&1
cmp /dev/null stdout2.txt
diff -u - subbed.txt <<'EOF'
LINE1
EOF

# 3.  The wfile is CREATED BEFORE PROCESSING BEGINS.  Put something in it, then run a
#     script that names it over an EMPTY input: nothing is ever written, and the file must
#     still come back empty because fcomp() truncated it while reading the script.
echo "stale contents" >picked.txt
env -i "$sim" "$prog" -n -f writef.sed /dev/null >stdout3.txt 2>&1
cmp /dev/null stdout3.txt
cmp /dev/null picked.txt

# 4.  Two w commands naming the same file share one descriptor, so the lines arrive in
#     order.  Written here rather than checked in, because the point is the pair.
cat >both.sed <<'EOF'
/line1/w shared.txt
/line2/w shared.txt
EOF
env -i "$sim" "$prog" -n -f both.sed nums.txt >stdout4.txt 2>&1
cmp /dev/null stdout4.txt
diff -u - shared.txt <<'EOF'
line1
line2
EOF

# 5.  ... and that really is one descriptor and not two that happen to agree: with two
#     streams onto one file each would start at offset 0 and the second would overwrite
#     the first, so a two-line answer is only possible from a shared one.
lines=$(wc -l <shared.txt)
if [ "$lines" -ne 2 ]; then
    echo "shared.txt has $lines lines, expected 2 -- the two w commands did not share a stream" >&2
    exit 1
fi

echo "cmd_sed_files: w and s///w reach their files, and a wfile is truncated at compile time"
