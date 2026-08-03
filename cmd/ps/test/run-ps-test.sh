#!/bin/sh
# The FORMAT oracle for ps(1), under b6sim.  Task C8.  Invoked by ctest as:
#
#	run-ps-test.sh SIM PROG
#
# WHY THIS IS NOT A SET OF b6_progtest CASES.  Every interesting field of a ps line is a
# number that is different on every run and on every machine -- the pid, the parent's pid,
# the uid.  b6sim's imitation kernel takes all three from the host (cmd/sim/kernel.cpp), on
# purpose: a table that is self-consistent and wrong passes every internal check, and the
# only thing that catches it is a value the program already knew from somewhere else.  That
# is the same argument lib/test/kctlt.c makes, and this script is the same trick from the
# outside -- the SHELL knows what `id -u' says, so it can check the UID column; it knows
# what it just ran, so it can check the CMD column.
#
# WHAT b6sim CANNOT SHOW is plurality: proc[] holds one live entry, the guest itself, so
# there is exactly one line to look at, its state is always R, its flags always 01 (SLOAD)
# and its WCHAN always blank.  More than one process, a real WCHAN, a nonzero TIME and a
# swapped-out row are kernel/test/inspect's, on the booted image.
#
# ENV -i, for the reason ../../pr/test/run-pr-test.sh gives.
set -e
sim=$1
prog=$2

LONGHDR=' F S UID   PID  PPID CPU PRI NICE    ADDR     SZ   WCHAN TTY TIME CMD'
SHORTHDR='   PID TTY TIME CMD'

fail() {
    echo "cmd_ps_format: $1" >&2
    shift
    for f; do
        echo "--- $f" >&2
        cat "$f" >&2
    done
    exit 1
}

# 1.  The short report.  Two lines: the header, and the guest's own row.
env -i "$sim" "$prog" >out1.txt 2>&1
[ "$(wc -l <out1.txt)" -eq 2 ] || fail "the short report is not two lines" out1.txt
[ "$(sed -n 1p out1.txt)" = "$SHORTHDR" ] || fail "the short header is wrong" out1.txt
row=$(sed -n 2p out1.txt)

# PID, right-justified in six.  It has to be OUR pid, and the only handle the shell has on
# that is that b6sim's guest pid IS b6sim's own pid -- so compare the two ranges rather than
# the numbers, and leave the exact value to the image.
case "$row" in
[\ 0-9][\ 0-9][\ 0-9][\ 0-9][\ 0-9][0-9]" "*) ;;
*) fail "the PID column is not six characters of right-justified decimal" out1.txt ;;
esac
# TTY is the index of u_ttyp in sc[]; b6sim points it at sc[0].
[ "$(echo "$row" | cut -c7-9)" = " 0 " ] || fail "the TTY column is not sc[0]" out1.txt
# TIME is mm:ss and the guest has burnt no measurable CPU under a simulator that bills none.
[ "$(echo "$row" | cut -c10-15)" = "  0:00" ] || fail "the TIME column is not 0:00" out1.txt
# CMD is u_comm, which exec() filled with the base name of what was run.
[ "$(echo "$row" | cut -c17-)" = "ps" ] || fail "the CMD column is not the program's name" out1.txt

# 2.  The long report.  Same row, eight more columns in front of it.
env -i "$sim" "$prog" -l >out2.txt 2>&1
[ "$(wc -l <out2.txt)" -eq 2 ] || fail "the long report is not two lines" out2.txt
[ "$(sed -n 1p out2.txt)" = "$LONGHDR" ] || fail "the long header is wrong" out2.txt
row=$(sed -n 2p out2.txt)

# F is p_flag in octal: SLOAD (01) and nothing else, the guest being in core and not a
# system process, not locked, not swapping and not traced.
[ "$(echo "$row" | cut -c1-2)" = " 1" ] || fail "the F column is not 01 (SLOAD)" out2.txt
# S is states[p_stat]: SRUN.
[ "$(echo "$row" | cut -c4)" = "R" ] || fail "the S column is not R" out2.txt
# UID: the one thing here the shell independently knows.
want=$(id -u)
[ "$(echo "$row" | cut -c5-8 | sed 's/^ *//')" = "$want" ] ||
    fail "the UID column is not $want" out2.txt
# ADDR is a WORD address in OCTAL, not v7's hex click address.  b6sim puts the imaginary
# image at KREACH, which is 0100000 -- so this column also pins the base.
[ "$(echo "$row" | cut -c34-41)" = "  100000" ] || fail "the ADDR column is not 0100000" out2.txt
# WCHAN is blank for a process that is not asleep.
[ "$(echo "$row" | cut -c49-56)" = "        " ] || fail "the WCHAN column is not blank" out2.txt
[ "$(echo "$row" | cut -c58-)" = "0   0:00 ps" ] ||
    fail "the tail of the long row is not TTY/TIME/CMD" out2.txt

# 3.  -a and -x widen the selection and must not change the shape of it: there is only one
#     process here, so all three reports have the same single row.
env -i "$sim" "$prog" -alx >out3.txt 2>&1
[ "$(wc -l <out3.txt)" -eq 2 ] || fail "-alx is not two lines" out3.txt
[ "$(sed -n 1p out3.txt)" = "$LONGHDR" ] || fail "-alx did not take the l" out3.txt
[ "$(sed -n 2p out2.txt | cut -c1-8)" = "$(sed -n 2p out3.txt | cut -c1-8)" ] ||
    fail "-alx changed the row" out2.txt out3.txt

echo "cmd_ps_format: both headers are exact, the UID column is what id(1) says, ADDR is an"
echo "               octal word address at KREACH, WCHAN is blank for a running process and"
echo "               CMD comes out of u_comm with no argv reconstruction anywhere"
