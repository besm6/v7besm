# /bin/vmstat -- 2.11BSD's vmstat(1), built natively for the BESM-6.
#
# NOT A v7 PROGRAM.  v7 has no vmstat at all; the source is RetroBSD's
# (src/cmd/vmstat.c), by way of 2.11BSD, and vmstat.c's header is the account of what
# survived the crossing and what did not.  What brought it here is kernel/kctl.c: its
# table had been holding a dozen live counters open on the sentence "they come with a
# vmstat", and this is the vmstat.
#
# /bin AND NOT /etc, which is where 2.11BSD had it and is cmd/dmesg's argument rather
# than iostat's: this asks kctl(2) for every number it prints and opens no device, so it
# is an ordinary user command and its page is section 1.  iostat and pstat are in /etc
# because v7's pages for them are section 1M, which is a fact about v7 and not about
# privilege -- neither of those needs any either.  The side effect is worth knowing: the
# two hard-coded `ls /bin' expectations (kernel/test/console.ini and
# kernel/test/session.expected) DO have to carry this program, and do.
#
# NOT SETUID, and there is nothing for it to be setuid to.
#
# THE KERNEL SIDE CAME WITH IT.  kernel/dev/md.c and kernel/dev/mb.c now keep dk_busy,
# dk_numb[] and dk_wds[], which nothing had ever written, and kernel/intr.c,
# syscall.c, trap.c and slp.c keep nintr, nsyscall, ntrap and nswtch -- which is where
# the `disks' and `faults' columns come from.  cmd/iostat prints three columns per
# device out of the same histogram; this one prints one, and neither is the other's
# summary.
#
# vmstat.1 is the manual page; nothing installs it, it is read with `nroff -man
# vmstat.1'.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(vmstat DEST bin/vmstat SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/vmstat.c)

add_subdirectory(test)
