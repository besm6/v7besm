#!/bin/sh
# The CROSS-REFERENCE oracle for pstat(1M), under b6sim.  Task C8.  Invoked by ctest as:
#
#	run-pstat-test.sh SIM PROG
#
# THE SIX EMPTY TABLES ARE LITERALS and are b6_progtest cases next door -- inode, file, text,
# mount and both allocation maps are zero on purpose in b6sim's imitation kernel, and the
# terminal array is two all-zero rows, which is what pins NSC and the 29-word tty stride.
# This script is for the two modes whose numbers the host supplies: -p, whose uid, pgrp, pid
# and ppid are the build machine's, and -u, whose uids are too.
#
# WHAT IT IS ACTUALLY FOR, though, is neither of those.  It is the CROSS-REFERENCE: `pstat -p'
# prints a LOC computed as kgetsym("proc") + i * (sizeof(struct proc)/NBPW), and `pstat -u 0'
# prints the live u-area's procp, which the kernel filled in with a real pointer to the entry
# that process occupies.  The two must be the same number.  That is the check
# lib/test/kctlt.c makes from inside the kernel's own headers, and a table pointing at the
# wrong array, or at the right array with the wrong stride, passes every other test here and
# fails this one.
#
# ENV -i, for the reason ../../pr/test/run-pr-test.sh gives.
set -e
sim=$1
prog=$2

fail() {
    echo "cmd_pstat_tables: $1" >&2
    shift
    for f; do
        echo "--- $f" >&2
        cat "$f" >&2
    done
    exit 1
}

# 1.  The process table.  One live entry under b6sim, and it is the guest.
env -i "$sim" "$prog" -p >outp.txt 2>&1
[ "$(sed -n 1p outp.txt)" = "1 processes" ] || fail "-p does not report one process" outp.txt
[ "$(sed -n 2p outp.txt)" = "     LOC S  F  PRI SIGNAL  UID TIM CPU NI  PGRP   PID  PPID    ADDR   SIZE   WCHAN    LINK   TEXTP CLKT" ] ||
    fail "the -p header is wrong" outp.txt
[ "$(wc -l <outp.txt)" -eq 3 ] || fail "-p is not three lines" outp.txt
row=$(sed -n 3p outp.txt)

loc=$(echo "$row" | cut -c1-8 | sed 's/^ *//')
[ -n "$loc" ] || fail "-p printed no LOC" outp.txt
# S is SRUN (3), F is SLOAD (01).
[ "$(echo "$row" | cut -c9-13)" = " 3  1" ] || fail "-p's S and F are not 3 and 01" outp.txt
# ADDR is a word address in octal at KREACH; SIZE is b6sim's whole memory, in words.
[ "$(echo "$row" | cut -c60-74)" = "  100000  32768" ] ||
    fail "-p's ADDR/SIZE are not 0100000 and 32768" outp.txt

# 2.  -a shows every slot, live or not, and there are NPROC of them.
env -i "$sim" "$prog" -ap >outa.txt 2>&1
[ "$(wc -l <outa.txt)" -eq 152 ] || fail "-ap is not NPROC + 2 lines" outa.txt

# 3.  THE CROSS-REFERENCE.  u_procp out of the live u-area must be the LOC of slot 0.
env -i "$sim" "$prog" -u 0 >outu.txt 2>&1
grep -q '^u-area at 74000 (the live one)$' outu.txt ||
    fail "-u 0 did not read the live u-area at UBASE" outu.txt
procp=$(sed -n 's/^procp \(.*\)$/\1/p' outu.txt)
[ "$procp" = "$loc" ] ||
    fail "u_procp is $procp but -p puts slot 0 at $loc" outu.txt outp.txt
# u_comm is the program's own name, and the terminal pointer is sc[0], not null.
grep -q '^comm pstat$' outu.txt || fail "-u 0 did not find u_comm" outu.txt
[ "$(sed -n 's/^ttyp \(.*\)$/\1/p' outu.txt)" != "0" ] || fail "-u 0 found a null u_ttyp" outu.txt

# 4.  The clock.  time(2) and the kctl value must agree to within a second or two; the point
#     of the mode is lbolt, which is the fraction time(2) cannot report.
env -i "$sim" "$prog" -c >outc.txt 2>&1
[ "$(wc -l <outc.txt)" -eq 2 ] || fail "-c is not two lines" outc.txt
t=$(sed -n 's/^time \([0-9]*\).*$/\1/p' outc.txt)
now=$(date +%s)
[ -n "$t" ] || fail "-c printed no time" outc.txt
[ "$((now - t))" -ge -2 ] && [ "$((now - t))" -le 2 ] ||
    fail "-c's time is $t but the host says $now" outc.txt
grep -q '^time [0-9]*  lbolt [0-9]*/250 second$' outc.txt ||
    fail "-c's lbolt is not a fraction of HZ" outc.txt

echo "cmd_pstat_tables: -p's LOC arithmetic and the live u-area's u_procp name the same slot,"
echo "                  -a walks all NPROC of them, and -c's clock agrees with the host's"
