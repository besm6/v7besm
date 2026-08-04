#!/bin/sh
# The FORMAT oracle for vmstat(1), under b6sim.  Invoked by ctest as:
#
#	run-vmstat-test.sh SIM PROG
#
# WHY THIS IS NOT A SET OF b6_progtest CASES, which is what the badopt case beside it is.
# Every rate vmstat prints is divided by the ticks in dk_time[], and b6sim derives those
# from its own INSTRUCTION COUNTER at one tick per thousand instructions
# (cmd/sim/kernel.cpp).  So the denominator at the moment of the read is 0 or 1 or 2
# depending on how far the program has got, and 0 divides differently from 1: the first
# report comes back all zeros and the second, having done a sleep(2) and a second pass,
# comes back with a system-call rate and a user percentage.  BOTH ARE CORRECT, and neither
# may be checked in as a literal, because the boundary between them is a property of the
# code generator rather than of this program.  cmd/iostat/test says the same thing about
# the same counter.
#
# So the assertion is: the HEADERS are exact, every COLUMN is where and as wide as it should
# be and holds an integer, the CPU percentages are a PARTITION, and the columns b6sim cannot
# move are ZERO.
#
# THE ZEROS ARE THE HONESTY CHECK AND NOT AN OMISSION.  b6sim has one process, no block
# device, no 0500 vector and no scheduler, so `b', `w', `md', `mb', `in', `tr' and `cs' are
# structurally zero -- and a harness that invented traffic for them would be worse than one
# that reported none.  `r' is at least 1, the guest itself being runnable, and `sy' moves
# because b6sim dispatched those calls itself.  Where the rest move is kernel/test/inspect,
# on the booted image, and that is the whole of what the boot half of this testing is for.
#
# ROUNDING.  The four CPU percentages are rounded independently, so they may sum to 99 or
# 101; the tolerance below is +-2 and it is deliberate, not slack.
#
# ENV -i, for the reason ../../pr/test/run-pr-test.sh gives.
set -e
sim=$1
prog=$2

fail() {
    echo "cmd_vmstat_format: $1" >&2
    shift
    for f; do
        echo "--- $f" >&2
        cat "$f" >&2
    done
    exit 1
}

# Every field of a data line, cut by COLUMN: the fields have no separators between them
# (`  0  0' is two columns, not one number), so splitting on whitespace would not see a
# column that had run into its neighbour.  `spec' is name:start:width, space-separated.
check_fields() {
    awk -v where="$2" -v spec="$3" '
        {
            n = split(spec, f, " ")
            for (i = 1; i <= n; i++) {
                split(f[i], p, ":")
                v = substr($0, p[2], p[3])
                if (v !~ /^ *-?[0-9]+$/) {
                    printf "%s: column %s is not an integer: [%s]\n", where, p[1], v
                    exit 1
                }
            }
        }
    ' "$1" || exit 1
}

# One named field of a data line, as a number.
field() {
    awk -v s="$2" -v w="$3" '{ print substr($0, s, w) + 0 }' "$1"
}

# The CPU percentages must be a partition, to within the independent rounding.
check_pct() {
    awk -v where="$2" -v start="$3" -v ncol="$4" '
        {
            tot = 0
            for (i = 0; i < ncol; i++)
                tot += substr($0, start + i * 4, 4) + 0
            if (tot != 0 && (tot < 98 || tot > 102)) {
                printf "%s: the cpu percentages sum to %d, not 100 or 0\n", where, tot
                exit 1
            }
        }
    ' "$1" || exit 1
}

# ---- 1.  The default report ------------------------------------------------------------
env -i "$sim" "$prog" >out1.txt 2>&1
[ "$(wc -l <out1.txt)" -eq 3 ] || fail "the default report is not three lines" out1.txt
[ "$(sed -n 1p out1.txt)" = " procs       memory       disks      faults         cpu" ] ||
    fail "line 1 is not the five group banners" out1.txt
[ "$(sed -n 2p out1.txt)" = " r  b  w      avm     fre  md  mb   in   sy   cs  us  sy  id" ] ||
    fail "line 2 is not the thirteen column headings" out1.txt
sed -n 3p out1.txt >d1.txt
[ "$(wc -c <d1.txt)" -eq 61 ] || fail "the data line is not 60 characters wide" d1.txt
check_fields d1.txt "the default report" \
    "r:1:2 b:3:3 w:6:3 avm:9:9 fre:18:8 md:26:4 mb:30:4 in:34:5 sy:39:5 cs:44:5 us:49:4 sy:53:4 id:57:4"
check_pct d1.txt "the default report" 49 3

# The guest itself is runnable, and nothing else exists to be blocked or swapped out.
[ "$(field d1.txt 1 2)" -ge 1 ] || fail "nothing is runnable, not even vmstat" d1.txt
[ "$(field d1.txt 3 3)" -eq 0 ] || fail "b is not zero, and b6sim has one process" d1.txt
[ "$(field d1.txt 6 3)" -eq 0 ] || fail "w is not zero, and b6sim has no swap" d1.txt

# The columns b6sim structurally cannot move.  A number here means the imitation kernel has
# started inventing traffic, which is the one failure this half of the testing exists for.
[ "$(field d1.txt 26 4)" -eq 0 ] || fail "md moved, and b6sim has no disk" d1.txt
[ "$(field d1.txt 30 4)" -eq 0 ] || fail "mb moved, and b6sim has no drum" d1.txt
[ "$(field d1.txt 34 5)" -eq 0 ] || fail "in moved, and b6sim takes no interrupts" d1.txt
[ "$(field d1.txt 44 5)" -eq 0 ] || fail "cs moved, and b6sim has one process" d1.txt

# ---- 2.  -p: four more columns, the same partition ---------------------------------------
env -i "$sim" "$prog" -p >out2.txt 2>&1
[ "$(sed -n 1p out2.txt)" = " procs        memory         swap   disks         faults             cpu" ] ||
    fail "-p line 1 is not the six group banners" out2.txt
[ "$(sed -n 2p out2.txt)" = " r  b  w     avm txt    fre   i   o  md  mb   in   sy   tr   cs  us  ni  sy  id" ] ||
    fail "-p line 2 is not the eighteen column headings" out2.txt
sed -n 3p out2.txt >d2.txt
[ "$(wc -c <d2.txt)" -eq 80 ] || fail "-p's data line is not 79 characters wide" d2.txt
check_fields d2.txt "-p" \
    "r:1:2 b:3:3 w:6:3 avm:9:8 txt:17:4 fre:21:7 i:28:4 o:32:4 md:36:4 mb:40:4 in:44:5 sy:49:5 tr:54:5 cs:59:5 us:64:4 ni:68:4 sy:72:4 id:76:4"
check_pct d2.txt "-p" 64 4
[ "$(field d2.txt 54 5)" -eq 0 ] || fail "-p's tr moved, and b6sim has no 0500 vector" d2.txt

# ---- 3.  -s: the labels, and the one counter b6sim really keeps ---------------------------
#
# The NUMBERS are not checked and cannot be -- every one of them is a tick count or a call
# count.  The labels are, because a row dropped from the table or renamed in the kernel is
# exactly what would otherwise go unnoticed.
env -i "$sim" "$prog" -s >out3.txt 2>&1
[ "$(wc -l <out3.txt)" -eq 16 ] || fail "-s is not sixteen lines" out3.txt
sed 's/^ *[0-9]* //' out3.txt >lab3.txt
cat >want3.txt <<'EOF'
swap ins
swap outs
text images read in
text images written out
joins to a text already in core
cpu context switches
device interrupts
traps
system calls
bytes copied whole words at a time
bytes copied a byte at a time, in phase
bytes copied a byte at a time, out of phase
disk transfers
disk words moved
drum transfers
drum words moved
EOF
cmp -s lab3.txt want3.txt || fail "-s does not name the sixteen counters" lab3.txt want3.txt
# The one b6sim genuinely knows: it dispatched those calls itself.
[ "$(sed -n 9p out3.txt | awk '{print $1}')" -ge 1 ] ||
    fail "-s reports no system calls, and it took several to say so" out3.txt

# ---- 4.  An interval with a count, and the differential path -----------------------------
#
# The reports after the first are DIFFERENTIAL, which is the one path a single report never
# enters -- and it is where the shadow arrays are either right or silently wrong.  By the
# second report b6sim's tick counter has moved, so `sy' is a real rate rather than a zero
# denominator, and requiring it is what makes this more than a line count.
env -i "$sim" "$prog" 1 2 >out4.txt 2>&1
[ "$(wc -l <out4.txt)" -eq 4 ] || fail "an interval of 1 and a count of 2 is not four lines" out4.txt
sed -n 3p out4.txt >d3.txt
sed -n 4p out4.txt >d4.txt
check_pct d3.txt "the cumulative report" 49 3
check_pct d4.txt "the differential report" 49 3
[ "$(field d4.txt 39 5)" -ge 1 ] ||
    fail "the differential report shows no system calls at all" out4.txt

echo "cmd_vmstat_format: the headers are exact, every column is where and as wide as its"
echo "                   heading says, the cpu percentages partition 100, the columns"
echo "                   b6sim cannot move are zero, -s names all sixteen counters and an"
echo "                   interval with a count reports through the differential path"
