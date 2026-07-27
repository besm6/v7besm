# /etc/swap -- the load kernel/test/swap drives, run as sh /etc/swap.
#
# THIS FILE IS NOT IN root.manifest and is not part of the shipped root filesystem.
# run-swap.sh grafts it onto its own converted copy of the image with b6fsutil -a, which
# is what keeps a test fixture out of the tree the kernel really boots.  session.sh and
# libtest.sh get there the same way.
#
# The hash is a real comment: this shell has one, which v7's had not -- see
# cmd/sh/README.md.  These lines used to begin with `:', the null built-in, whose words
# were still parsed -- and all three scripts in this directory learned what that cost.
#
# WHAT THIS PROVES.  Every test above this one runs one program at a time in a machine with
# more core than it can use.  Here the .ini has deposited a much smaller phymem before the
# boot, so the coremap holds about thirty pages -- and this script then asks for more than
# that at once.  What has to happen, and what the .ini asserts on afterwards through the
# kernel counters:
#
#   * sched and newproc must SWAP.  nswapout and nswapin count it.  Three copies of puret
#     plus the shells that forked them do not fit, so newproc cannot malloc the child image
#     and takes its xswap arm, and sched picks a victim of its own while they wait.
#   * two processes must SHARE ONE BINARY S TEXT.  ntextjoin counts xalloc finding a text
#     already in the table.  Two images on the disk are PURE and can be shared at all --
#     /bin/sh and /usr/test/puret -- and both are shared here: the shell that runs this
#     script joins the one at the prompt, and the second and third puret join the first.
#     Every other program on the image is FMAGIC, for which getxfile forces the text size
#     to zero and xalloc returns at once.
#   * and the images must come back INTACT.  Each puret writes its own output file and
#     checks its own data segment on the way, so a page that came back from the drum wrong,
#     or a process resumed with somebody else s image, shows up as a diff on the host.

# Three copies at once, each with its own instance digit.  puret prints the digit out of
# its own data segment, so a kernel that let two processes share the DATA along with the
# text would have them report each other s numbers.

cd /usr/test
./puret 1 >/tmp/p1.out 2>&1 &
./puret 2 >/tmp/p2.out 2>&1 &
./puret 3 >/tmp/p3.out 2>&1 &

# wait sleeps at PWAIT, which is above PZERO, so the waiting shell is a process sleeping at
# a BAD priority -- which is exactly what sched looks for first when it needs to free core.
# Without this line the only swapper arm exercised would be newproc s.

wait

# A file big enough to need its inode s indirect block, written while all that is going on,
# so the host has something to compare byte for byte afterwards.  33 KB of /bin/ls against
# six direct blocks of 3072 bytes each -- the same size argument session.sh makes.

cat /bin/ls >/tmp/big
ls /tmp >/tmp/swap.log

# sync LAST.  There is no update daemon on this system, so without it the delayed-write
# buffers holding everything above are still in core when the simulator stops.

sync
echo swap done >/dev/console
