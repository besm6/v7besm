#!/bin/sh
# The FORMAT oracle for iostat(1M), under b6sim.  Task C8.  Invoked by ctest as:
#
#	run-iostat-test.sh SIM PROG
#
# WHY THIS IS NOT A SET OF b6_progtest CASES, which is what the rest of this task's b6sim
# testing is.  Every number iostat prints is derived from dk_time[], and b6sim derives
# dk_time[0] from its own INSTRUCTION COUNTER at a resolution of one tick per thousand
# instructions (cmd/sim/kernel.cpp).  So the tick total at the moment of the read is 0 or 1
# or 2 depending on how far the program has got, and 0 divides differently from 1: with the
# header printed first the CPU line reads `100.00  0.00  0.00  0.00', and with -i, which
# prints no header, the same four values come back all zero because the denominator has not
# reached 1 yet.  BOTH ARE CORRECT -- v7's iostat guards a zero denominator the same way and
# prints the same zeros -- and neither may be checked in as a literal, because the boundary
# between them is a property of the code generator rather than of this program.
#
# So the assertion is: the HEADERS are exact, the COLUMNS are exactly as wide and as many as
# they should be, and the four percentages are a PARTITION -- they sum to 100.00 or, when
# there is nothing yet to partition, to 0.00.  A column dropped, transposed or misformatted
# fails; a tick counter that has moved on does not.
#
# WHAT b6sim CANNOT SHOW AT ALL is a moving column.  There is no kernel under it to bill
# system time to, nothing idle and no nice, so every tick lands in dk_time[0] and `user' is
# 100.00 whenever it is anything.  A systm and an idle column with real numbers in them is
# kernel/test/inspect's, on the booted image, and that is the whole of what the boot half of
# this program's testing exists for.
#
# ENV -i, for the reason ../../pr/test/run-pr-test.sh gives.
set -e
sim=$1
prog=$2

fail() {
    echo "cmd_iostat_format: $1" >&2
    shift
    for f; do
        echo "--- $f" >&2
        cat "$f" >&2
    done
    exit 1
}

# The four percentages must be a partition.  Six characters each, `%3d.%02d', so the fields
# can run together (`   0.0100.00' is a tin column and a user column, not one number) and
# they are cut by COLUMN rather than split on whitespace.
check_pct() {
    awk -v where="$2" '
        {
            line = $0
            tot = 0
            for (i = 0; i < 4; i++) {
                f = substr(line, length(line) - 24 + i * 6 + 1, 6)
                if (f !~ /^ *[0-9]+\.[0-9][0-9]$/) {
                    printf "%s: column %d is not a percentage: [%s]\n", where, i + 1, f
                    exit 1
                }
                sub(/\./, "", f)
                tot += f + 0
            }
            if (tot != 10000 && tot != 0) {
                printf "%s: the four percentages sum to %d, not 10000 or 0\n", where, tot
                exit 1
            }
        }
    ' "$1" || exit 1
}

# 1.  The default report: two header lines and one data line.
env -i "$sim" "$prog" >out1.txt 2>&1
[ "$(wc -l <out1.txt)" -eq 3 ] || fail "the default report is not three lines" out1.txt
[ "$(sed -n 1p out1.txt)" = "        PERCENT" ] ||
    fail "line 1 is not the PERCENT banner" out1.txt
[ "$(sed -n 2p out1.txt)" = "  user  nice systm  idle" ] ||
    fail "line 2 is not the four CPU column headings" out1.txt
[ "$(sed -n 3p out1.txt | wc -c)" -eq 25 ] ||
    fail "the data line is not 24 characters wide" out1.txt
sed -n 3p out1.txt >d1.txt
check_pct d1.txt "the default report"

# 2.  -t puts the two terminal columns in front of them, and widens both headers by the same
#     twelve characters.  tin and tout are `%4d.%d' and are 0.0 here whatever else happens:
#     b6sim counts the bytes it really moved through descriptors 0, 1 and 2, and a stdout
#     that is a pipe has not been flushed when the read happens.
env -i "$sim" "$prog" -t >out2.txt 2>&1
[ "$(sed -n 1p out2.txt)" = "         TTY        PERCENT" ] ||
    fail "-t line 1 is not the TTY + PERCENT banner" out2.txt
[ "$(sed -n 2p out2.txt)" = "   tin  tout  user  nice systm  idle" ] ||
    fail "-t line 2 is not the six column headings" out2.txt
sed -n 3p out2.txt >d2.txt
[ "$(wc -c <d2.txt)" -eq 37 ] || fail "-t's data line is not 36 characters wide" d2.txt
grep -q '^ *[0-9]*\.[0-9] *[0-9]*\.[0-9]' d2.txt ||
    fail "-t's first two columns are not one-decimal rates" d2.txt
check_pct d2.txt "-t"

# 3.  -i names the same four numbers, one per line, in v7's order: idle, user, nice, system.
#     No header, and the labels are what distinguishes this from a transposed default report.
env -i "$sim" "$prog" -i >out3.txt 2>&1
[ "$(wc -l <out3.txt)" -eq 4 ] || fail "-i is not four lines" out3.txt
i=1
for want in idle user nice system; do
    got=$(sed -n "${i}p" out3.txt)
    case "$got" in
    *" $want") ;;
    *) fail "-i line $i does not end in '$want'" out3.txt ;;
    esac
    case "$got" in
    [\ 0-9][\ 0-9][\ 0-9].[0-9][0-9]\ *) ;;
    *) fail "-i line $i does not begin with a six-character percentage" out3.txt ;;
    esac
    i=$((i + 1))
done

# 4.  An interval with a count really does report that many times, and the reports after the
#     first are DIFFERENTIAL -- which is the one path a single report never enters.  Two
#     seconds of wall clock, and libc's sleep(3) under b6sim is the host's alarm/pause pair.
env -i "$sim" "$prog" 1 2 >out4.txt 2>&1
[ "$(wc -l <out4.txt)" -eq 4 ] || fail "an interval of 1 and a count of 2 is not four lines" out4.txt
sed -n 3p out4.txt >d3.txt
sed -n 4p out4.txt >d4.txt
check_pct d3.txt "the cumulative report"
check_pct d4.txt "the differential report"

echo "cmd_iostat_format: the headers are exact, the columns are the width v7 printed them,"
echo "                   the four percentages partition 100.00, -t prepends two rate columns"
echo "                   and an interval with a count reports through the differential path"
