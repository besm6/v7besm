# Standalone BESM-6 programs, run on the SIMH simulator (`besm6').
#
# Not host unit tests: each is a BESM-6 program that links kernel objects against a
# hand-built environment, plus a .ini that loads it into the simulator, runs it, and
# asserts on the machine state (each .ini `exit 1's on failure, so a nonzero exit fails
# the ctest case).  Reuses b6_obj/b6_find_src from the parent CMakeLists.
#
# Kernel objects are compiled HERE from the sources next door (never borrowed from
# kernel/'s own build), found by b6_find_src searching `. .. ../dev'.
#
# EVERYTHING here gets -DKERNEL, the local test programs as much as the kernel sources
# they link, because KERNEL means "this translation unit is kernel-side" and not "this
# object goes in the kernel image": these programs include <sys/systm.h> and so stand on
# the kernel's side of the gates in <sys/stat.h>, <sys/times.h> and <sys/wait.h>, where
# `stat', `times' and `wait' name syscall handlers rather than libc's leaves.  Until the
# headers were made self-contained those gates carried a second condition on systm.h's own
# include guard, which is what let this directory go without the flag; that was an include
# ORDER dependency in disguise, and it is gone (include/README.md).
#
# coninit is the exception: a USER-MODE program that runs under the booted kernel.  The .s
# members need no exclusion -- b6_obj routes those to b6as, which never sees ARGN.
set(USERSIDE coninit)

set(TEST_TARGETS "")

# Compile ${base}.o for program ${prog} into its OWN directory (${prog}.dir), with -DKERNEL
# for everything but the user-mode programs.  Objects are per-program on purpose: many images share a
# source (utab, brz, crt0, ...), and a single shared object OUTPUT would be emitted into every
# consuming target's build rules and race under `make -j' (MAKEFLAGS=-j8), corrupting the file
# a link then reads.  Recompiling a shared source once per image trades a little build time for
# a parallel-safe graph.  Sets ${outvar} to the object path.
function(b6_test_obj outvar prog base)
    b6_find_src(_src ${base})
    set(B6_OBJDIR ${CMAKE_CURRENT_BINARY_DIR}/${prog}.dir)
    if("${base}" IN_LIST USERSIDE)
        b6_obj(_o ${_src})
    else()
        b6_obj(_o ${_src} -DKERNEL)
    endif()
    set(${outvar} ${_o} PARENT_SCOPE)
endfunction()

# Link a test program from a list of object basenames, then the dis/nm postlude.  Objects are
# named by full path (they live in ${prog}.dir) and the tools run in the binary dir so the
# executable and its .dis/.nm land beside the .ini's cwd.  A macro, to append to TEST_TARGETS
# in directory scope.  besm6 is optional; the .ini test is registered only when present.
macro(b6_test prog)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${prog}.dir)
    set(_paths "")   # object paths, for the b6ld line and DEPENDS
    foreach(_b ${ARGN})
        b6_test_obj(_o ${prog} ${_b})
        list(APPEND _paths ${_o})
    endforeach()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${prog}
        COMMAND ${B6LD} ${_paths} -o ${prog} -L${B6LIBDIR} -lruntime
        COMMAND sh -c "${B6DISASM} -c ${prog} > ${prog}.dis"
        COMMAND sh -c "${B6NM} -n ${prog} > ${prog}.nm"
        DEPENDS ${_paths}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "b6ld ${prog}" VERBATIM)
    add_custom_target(${prog}_img DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${prog})
    list(APPEND TEST_TARGETS ${prog}_img)
    if(BESM6)
        add_test(NAME ${prog} COMMAND ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}/${prog}.ini
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(${prog} PROPERTIES LABELS kernel)
    endif()
endmacro()

# hello is the odd one out: it links no b$* helpers (no -lruntime) and its .nm is left in
# symbol order rather than address order.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/hello.dir)
b6_test_obj(_hello_o hello hello)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hello
    COMMAND ${B6LD} ${_hello_o} -o hello
    COMMAND sh -c "${B6DISASM} -c hello > hello.dis"
    COMMAND sh -c "${B6NM} hello > hello.nm"
    DEPENDS ${_hello_o}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ld hello" VERBATIM)
add_custom_target(hello_img DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hello)
list(APPEND TEST_TARGETS hello_img)
if(BESM6)
    add_test(NAME hello COMMAND ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}/hello.ini
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(hello PROPERTIES LABELS kernel)
endif()

# The forge tests and driver tests.  Object lists are the old Makefile's per-program rules.
b6_test(sctest   crt0 sctest)
b6_test(mmutest  crt0 mmutest mmuhelp utab brz uarea seg usermem)
b6_test(umem     crt0 umem mmuhelp utab brz usermem ucopy)
b6_test(uintr    crt0u uintr utab brz)
b6_test(utrap    crt0t utrap utab brz)
b6_test(usys     crt0s usys syscall utab brz usermem)
b6_test(usig     crt0sg usig syscall sendsig utab brz usermem)
b6_test(uclock   crt0c uclock clock intr utab brz)
b6_test(uswtch   crt0w uswtch switch uarea brz intr)
b6_test(usched   crt0w usched slp switch uarea brz intr utab seg malloc)
b6_test(uswap    crt0w uswap slp text bio mb malloc switch uarea seg brz intr utab)
b6_test(ugrow    crt0g ugrow utab brz seg)
b6_test(biotest  crt0 biotest bio utab brz)
b6_test(ugrp     crt0 ugrp intr)
b6_test(mbtest   crt0 mbtest mb intr brz)
b6_test(mdtest   crt0 mdtest md intr brz)
b6_test(fstest   crt0 fstest bio md alloc intr utab brz)

# ---------------------------------------------------------------------------------------
# coninit -- the /etc/init the root image used to carry (task 23).
# ---------------------------------------------------------------------------------------
#
# Unlike everything above, this is a USER-MODE program: it runs under the booted kernel,
# reached through the icode's `$77 SYS_exec', and is what turns the console into a
# controlling terminal by asking for one.  So it gets neither b6_test (which would
# register a .ini test of its own and link -lruntime) nor a crt0: the test crt0s here are
# supervisor-mode scaffolding and libc is task 25's business.  Its own object is the whole
# program, which is why `_start' must be its first text word -- b6ld takes a_entry from
# the start of text unless told otherwise (cmd/ld/output.c).
#
# IT IS NO LONGER ON THE DISK IMAGE and no test drives it.  Task 25b made ../../root.manifest
# name the real cmd/init, and kernel/test/console now holds its dialogue with /bin/sh
# instead.  coninit is still BUILT, and deliberately: it is the only user program on this
# system that links neither crt0 nor libc, so when the console dialogue breaks it is what
# says whether the fault is in the kernel's terminal path or in everything the shell drags
# in behind it.  Building it also keeps the source from rotting.
#
# To use it, put it back by hand rather than by editing the manifest -- the image stays as
# it ships and the copy is yours:
#
#     cp root.img bisect.img
#     b6fsutil -a bisect.img /etc/init coninit
#     b6fsutil -S --volume=3079 bisect.img root3079.disk
#
# then attach root3079.disk in place of root3072.disk and the `line> ' loop is back.  That
# is the same graft run-session.sh uses to get its script onto its own copy.
#
# Nothing depends on it now, hence the target of its own.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/coninit.dir)
b6_test_obj(_coninit_o coninit coninit)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/coninit
    COMMAND ${B6LD} ${_coninit_o} -o coninit
    COMMAND sh -c "${B6DISASM} -c coninit > coninit.dis"
    COMMAND sh -c "${B6NM} -n coninit > coninit.nm"
    DEPENDS ${_coninit_o}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ld coninit" VERBATIM)
# The target may NOT be called `coninit': a phony target whose name is also a file in the
# same directory is a rule make considers satisfied by the file being there.
add_custom_target(coninitprog ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/coninit)
list(APPEND TEST_TARGETS coninitprog)

# ---------------------------------------------------------------------------------------
# The root filesystem image fstest reads (task 19) and the kernel boots (tasks 20, 23).
# ---------------------------------------------------------------------------------------
#
# b6fsutil (cmd/fsutil/) builds a v7 filesystem in the FLAT container -- blocks of 512
# six-byte words -- and `-S' converts that into the one SIMH attaches, which stores each
# word as eight little-endian bytes with a two-bit tag and interleaves eight service words
# per zone.  Both are kept: the flat one is what `-c' checks and what `od' can read, the
# SIMH one is what the simulator gets.
#
# b6fsutil is a HOST tool, not part of the cross toolchain, so it is resolved here rather
# than in scripts/BesmCross.cmake -- but with the same two modes: the in-tree target when
# this directory is configured inside the top-level project, the installed tool when the
# kernel is configured on its own (`cmake -S kernel').  Naming the target through
# $<TARGET_FILE:> also gives the custom command its build-order dependency for free.
if(TARGET b6fsutil)
    set(B6FSUTIL $<TARGET_FILE:b6fsutil>)
else()
    find_program(B6FSUTIL b6fsutil REQUIRED)
endif()

# The volume number is the rightmost run of digits in the SIMH filename, 2048..4095 --
# scraped by `attach' itself, so the name IS the volume.  This project numbers its images
# from 3072 up, well clear of the real DISPAK volumes down at 2048-2063; 3072 keeps clear
# of mdtest's 3073-3076.  ROOTTIME is the -T stamp: it makes the image byte-reproducible,
# and fstest.c asserts on it (its ROOTTIME must match this one) so that a stale or hand-made file
# cannot pass for the image this build just wrote.  -s 2000 is one EC-5052 drive, the
# default, spelled out because fstest.c checks s_fsize against it.
#
# THE STAMP IS ALSO THE BOOT CLOCK, which is why it is a date somebody chose rather than a
# round number.  This machine has no clock-calendar a program can read (<sys/param.h>), so
# main.c's iinit() seeds `time' from the superblock's s_time -- and that is this value.  It
# is 2026-07-25 08:23:00 GMT, the day this Unix first booted to a shell prompt.  The guest
# prints GMT and only GMT (TIMEZONE and DSTFLAG are both 0), so `date' on the image says
# `Sat Jul 25 08:23:0X GMT 2026'; the seconds field is the only one the boot drift can move,
# and 08:23:00 is 37 seconds clear of a minute roll.
set(ROOTTIME 1784967780)

#
# WHAT root.img IS BUILT OUT OF.  Every `file' entry in the manifest names a path under
# build/rootfs/ -- relative to this directory, since that is b6fsutil's cwd.  Listing those
# files here is what makes editing a command, or the motd, rebuild the image; the
# build-ORDER edge (rootfs must be staged before b6fsutil runs) cannot be drawn here at
# all, because `rootfs' does not exist yet when this directory is configured, and is drawn
# instead at the foot of the top-level CMakeLists.txt.
#
# Adding a program to the image therefore means two edits: the manifest, and this list.
#
# THE MANIFEST LIVES AT THE TOP OF THE TREE, not here, since task 25b: it describes a
# build/rootfs/ that cmd/ and etc/ stage, and no line of it is kernel/test's any more.  Its
# own header says so, and says why b6fsutil's working directory stayed put anyway.  Named
# by a path relative to THIS directory so that a standalone `cmake -S kernel' still
# configures.
get_filename_component(ROOT_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/../../root.manifest ABSOLUTE)

set(ROOTFS_FILES
    ${B6_ROOTFS}/etc/init
    ${B6_ROOTFS}/etc/getty
    ${B6_ROOTFS}/etc/quot
    ${B6_ROOTFS}/etc/group
    ${B6_ROOTFS}/etc/motd
    ${B6_ROOTFS}/etc/passwd
    ${B6_ROOTFS}/etc/rc
    ${B6_ROOTFS}/etc/termcap
    ${B6_ROOTFS}/etc/ttys
    ${B6_ROOTFS}/bin/sh
    ${B6_ROOTFS}/bin/basename
    ${B6_ROOTFS}/bin/cat
    ${B6_ROOTFS}/bin/chgrp
    ${B6_ROOTFS}/bin/chmod
    ${B6_ROOTFS}/bin/chown
    ${B6_ROOTFS}/bin/cp
    ${B6_ROOTFS}/bin/date
    ${B6_ROOTFS}/bin/df
    ${B6_ROOTFS}/bin/du
    ${B6_ROOTFS}/bin/echo
    ${B6_ROOTFS}/bin/ed
    ${B6_ROOTFS}/bin/kill
    ${B6_ROOTFS}/bin/ln
    ${B6_ROOTFS}/bin/login
    ${B6_ROOTFS}/bin/ls
    ${B6_ROOTFS}/bin/mkdir
    ${B6_ROOTFS}/bin/mv
    ${B6_ROOTFS}/bin/pwd
    ${B6_ROOTFS}/bin/rm
    ${B6_ROOTFS}/bin/rmdir
    ${B6_ROOTFS}/bin/sleep
    ${B6_ROOTFS}/bin/sync
    ${B6_ROOTFS}/bin/test
    ${B6_ROOTFS}/bin/time
    ${B6_ROOTFS}/bin/touch
    ${B6_ROOTFS}/bin/tty
    ${B6_ROOTFS}/bin/yes)
# /bin/[ is deliberately absent: it is a `link' stanza in the manifest, not a staged file,
# so there is nothing here for the image to depend on.  rootimg_link below is what holds it.

# ...and /usr/test, the lib/test programs (task 25c).  The list is lib/test's, since that is
# where they are linked and staged; naming it from one file is what keeps the manifest, this
# dependency list and the ctest cases below from drifting apart.  Read by a path relative to
# this directory so that a standalone `cmake -S kernel' still configures -- it cannot build
# root.img anyway, for the reason ../../root.manifest's header gives.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/test/progs.cmake)
foreach(_p ${B6_LIBTEST_IMAGE})
    list(APPEND ROOTFS_FILES ${B6_ROOTFS}/usr/test/${_p})
endforeach()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/root.img
    COMMAND ${B6FSUTIL} -n -s 2000 -T ${ROOTTIME}
            -M ${ROOT_MANIFEST} root.img
    DEPENDS ${ROOT_MANIFEST} ${ROOTFS_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil root.img" VERBATIM)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/root3072.disk
    COMMAND ${B6FSUTIL} -S root.img root3072.disk
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/root.img
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil -S root3072.disk" VERBATIM)

add_custom_target(rootimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/root3072.disk)
list(APPEND TEST_TARGETS rootimg)

# The image is self-consistent before any kernel touches it.  Cheap, and it keeps a
# b6fsutil regression from arriving disguised as a driver bug in fstest.  Not guarded by
# BESM6: it is a host check on a host tool's output, and runs whether or not the simulator
# is installed.
add_test(NAME rootimg_check COMMAND ${B6FSUTIL} -c root.img
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rootimg_check PROPERTIES LABELS kernel)

# THE SETUID BIT, READ BACK OFF THE FINISHED IMAGE (task C1a, and /bin/mv since C1b).
# /bin/mkdir, /bin/mv and /bin/rmdir are the only files here whose mode the staging tree
# cannot carry -- build/rootfs/bin/mkdir is an ordinary 0755 build artifact, and 04755 exists
# in ../../root.manifest and nowhere else -- so nothing but the image itself can say whether
# ISUID survived.  `-v -v' prints one line per path: type, mode in octal, uid/gid, size,
# inode and link count.
#
# WHAT THIS SEPARATES.  A setuid failure has two possible homes: b6fsutil dropping the bit
# when it writes the inode, and getxfile() ignoring it when it execs.  lib/test/suidt covers
# the second, but only under a booted kernel; this one covers the first, costs milliseconds,
# and -- like rootimg_check, and unlike everything below -- runs whether or not SIMH is
# installed.  Grep rather than a diff, so that a size or an i-number in the same line does
# not make it a maintenance burden.
add_test(NAME rootimg_setuid
    COMMAND sh -c "${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/mkdir$' && \
                   ${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/mv$' && \
                   ${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/rmdir$'"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rootimg_setuid PROPERTIES LABELS kernel)

# THE HARD LINK, READ BACK OFF THE FINISHED IMAGE (task C2b).  /bin/[ is /bin/test -- the
# same inode under two names -- and it is the only link on this image and the only entry in
# ../../root.manifest that names no `source'.  Nothing in build/rootfs/ corresponds to it, so
# it is invisible to ROOTFS_FILES above and to every size check; the image is again the only
# thing that can say whether it is there.
#
# WHY IT MATTERS RATHER THAN BEING DECORATION: cmd/test/test.c looks at argv[0] and requires a
# closing `]' when it was entered by that name, so with no link that branch is unreachable and
# `[ -f x ]' in a shell script is a command not found.  kernel/test/utils.sh types it.
#
# The two assertions are `both paths report nlink 2' and `everything before the path is
# identical between them' -- which is type, mode, owner, size and INODE, so the second is what
# actually says they are one file.  Same shape as rootimg_setuid: it runs whether or not SIMH
# is installed, and it costs milliseconds.
add_test(NAME rootimg_link
    COMMAND sh -c "${B6FSUTIL} -v -v root.img | grep -E ' /bin/(test|\\[)$' | grep -c 'nlink 2 ' | grep -qx 2 && \
                   ${B6FSUTIL} -v -v root.img | grep -E ' /bin/(test|\\[)$' | sed 's| /bin/.*||' | sort -u | grep -c . | grep -qx 1"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rootimg_link PROPERTIES LABELS kernel)

# ---------------------------------------------------------------------------------------
# boot, console, session and libtest -- the whole kernel booting with the root image
# attached (tasks 20, 23, 25b, 25c).
# ---------------------------------------------------------------------------------------
#
# None of the four links anything of its own: each loads the real `unix' image built one
# directory up by kernel/CMakeLists.txt, against the filesystem fstest reads.  They go one
# step further than the last at every stage, which is what keeps a failure diagnosable:
#
#   boot     process 1 leaves the kernel, execs /etc/init, init forks /bin/sh and the shell
#            PROMPTS -- rather than panicking (no root fs), falling into the icode's failure
#            loop, stopping anywhere in the kernel or hanging.  Still the regression guard
#            for the b_un.b_addr fat-pointer fix and for the whole path into user mode.
#   console  types at that shell: erase, kill, a line longer than a clist block, /dev/tty,
#            two commands b6sim cannot run, and ^D -- after which init loops through
#            /etc/rc and prompts again.
#   session  writes files, runs sync(2), and then the HOST fscks what reached the disk and
#            diffs a file the session wrote.  See run-session.sh.
#   libtest  runs the twenty-six /usr/test programs off the image and diffs each one's
#            output against the very file b6sim is held to.  Everything above it is the
#            kernel's own code exercising itself; this is user programs written against
#            libc, so it is where sysent[]'s rows meet a real caller.  See run-libtest.sh.
#
# WHICH DISK EACH ONE ATTACHES.  root3072.disk is the pristine build artifact and nothing
# writes it: `boot' attaches it -r, which is an assertion in itself -- the boot path needs no
# writes.  The other three need a writable unit and get a converted copy of root.img at a volume
# number of their own (3072 is the artifact, mdtest owns 3073-3076).  For `console' that is
# not because it writes files but because it execs six programs, and an i_atime write-back
# forced by buffer pressure must not land on a read-only unit and put an `md' error message
# into the very stream the expects are matching.
#
# THEY DO NOT RUN CONCURRENTLY WITH EACH OTHER, and the RESOURCE_LOCK below is why.  Each of
# these nine drives the guest by typing at it -- `expect "# " send "sh /etc/files\r"' -- and
# under load SIMH DROPS CHARACTERS out of that send: the failures read `h: not found' for a
# sent `sh' and `eco: not found' for an `echo', so the shell is being handed a truncated line
# and the test then fails as "never reached its last line".  Nothing in the guest, in the disk
# images or in the volume numbering is shared -- each test has its own of all three; it is the
# HOST that is contended, which is the whole of what has been established.  (The mechanism is
# not: the likely one is that `send' delivers at an instruction-count rate while the console's
# own timing is calibrated against the host clock, so oversubscribing the machine shifts the
# ratio between them -- but that has not been confirmed, and the lock treats the symptom.)
#
# SINCE TASK C2c THERE IS A SECOND CASE, and it is NOT load-dependent: console.ini dropped the
# FIRST character of one send on every run of an idle machine, and `send after=20000' fixed it.
# That file now carries the delay on every send; THE OTHER FIVE DO NOT.  So this lock has not
# been re-measured against a paced send, and it may be buying nothing -- ../TODO.md task 35 is
# that measurement, and it is the task that owns the whole question.
#
# AND SINCE TASK 29b A THIRD, which is the first one that points at a mechanism.  login.ini
# needed `delay=' -- the gap between the characters AFTER the first -- as well, and without it
# the SECOND character went, every run, on an idle machine.  What is different there is the
# guest: /etc/getty reads the login name in RAW mode, one read(2) and one write(2) per
# character from user mode, where every other file here types at a shell in canonical mode and
# the kernel fills a clist before waking anybody.  The same simulator at the same rate feeds a
# shell without loss and a getty with it, which is what an input overrun in dev/sc.c predicts
# and a host-clock artifact does not.  login.ini's header is the account.
#
# THE FIXED PORTS ARE ANOTHER REASON FOR THE LOCK, and a harder one: login.ini listens on 4199
# for the second Consul and multi.ini dials out to 4200, so two of these running at once would
# not merely drop a character, they would fail to bind.  Anything else that wants a port here
# must take the lock too.
#
# MEASURED, since a lock that bought nothing would be worth removing: five full `ctest' runs
# on one machine, twice.  Without it, two of the five failed this way and the suite took 47-49
# seconds; with it, none of five failed -- nor any of ten further `ctest -L kernel' runs -- and
# the suite takes about 62.  Fifteen seconds for a suite that does not lie.
#
# `simh_boot' is therefore a lock and not a fixture: they are independent tests that must not
# OVERLAP, which is a different statement from one needing another to have run.  ctest still
# runs everything else in parallel around them, and the standalone SIMH tests above (mmutest,
# fstest, the u* programs) are deliberately NOT in the lock -- they assert on machine state
# from their .ini rather than on a typed dialogue, so they have nothing to drop.
#
# `spin' is a LINKED address that moves as the kernel grows, so boot.ini is
# GENERATED from unix.nm at build time (genboot.cmake) rather than hardcoded.  console.ini
# and session.ini name no kernel address and are used from the source directory as they
# stand.  The generation depends on the `kernel' target so the image and its .nm exist first,
# and every test needs root.img/root3072.disk (rootimg) in its cwd -- all pulled in below.
if(BESM6)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot.ini
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
                "-DSYMS=spin"
                -DIN=${CMAKE_CURRENT_SOURCE_DIR}/boot.ini.in
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/boot.ini
                -P ${CMAKE_CURRENT_SOURCE_DIR}/genboot.cmake
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
                ${CMAKE_CURRENT_SOURCE_DIR}/boot.ini.in
                ${CMAKE_CURRENT_SOURCE_DIR}/genboot.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "gen boot.ini" VERBATIM)
    add_custom_target(bootini DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/boot.ini)
    add_dependencies(bootini kernel rootimg)  # the image to boot, and the disk to attach
    list(APPEND TEST_TARGETS bootini)
    add_test(NAME boot COMMAND ${BESM6} ${CMAKE_CURRENT_BINARY_DIR}/boot.ini
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(boot PROPERTIES LABELS kernel RESOURCE_LOCK simh_boot)

    # console.ini restarts the machine from inside each `expect' action, so its run is bounded
    # by wall clock rather than by one step budget: a TIMEOUT is what turns a wedged dialogue
    # into a failure.  It needs the kernel image and the disk just as `boot' does, which the
    # bootini target above already pulls in.  The TIMEOUT is per DIALOGUE, not per stage, so
    # it grows with the script: 300 seconds covered the eight stages of task 25b, and task C1a
    # added four more (mkdir, ls, rmdir, ls).
    #
    # Its own writable copy of the filesystem is made by a setup fixture, in the shape
    # mdtest_setup uses below.  `-S' takes the direction from the input, so this is the flat
    # image converted out to the SIMH container SIMH will attach; a fresh one every run means
    # a failed dialogue cannot poison the next.
    add_test(NAME console_setup COMMAND ${B6FSUTIL} -S --volume=3078 root.img root3078.disk
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(console_setup PROPERTIES FIXTURES_SETUP console_disk LABELS kernel)
    add_test(NAME console COMMAND ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}/console.ini
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(console PROPERTIES LABELS kernel TIMEOUT 450
                         FIXTURES_REQUIRED console_disk RESOURCE_LOCK simh_boot)

    #
    # DISABLED, AND NOT BECAUSE IT IS WRONG.  It fails about one run in three -- measured at
    # two failures in six on an otherwise untouched tree -- always the same way: the LAST
    # expect never fires, `Step expired' at PC 37037, with /etc/rc's motd printed and its
    # date line still to come.  Every other stage of the dialogue passes first.
    #
    # What makes that worth suspending rather than fixing here is that it is not
    # deterministic, and a SIMH step budget ought to be: the same image, the same script and
    # the same instruction count should reach the same place every time.  Something in the
    # run is taking its timing from the host -- the console poll is the obvious suspect --
    # and until that is understood, an intermittent failure in the one test everybody runs
    # teaches people to ignore a red result, which costs more than this test earns.
    #
    # NOTHING ELSE COVERS WHAT IT COVERS, so this is a debt and not a deletion: the typed
    # dialogue, erase and kill, a line longer than a clist block, /dev/tty from a forked
    # child, and the only assertion in the tree on /etc/rc's motd and boot date.  `login'
    # still types at a getty and `utils' still runs a script off the image, so the shell and
    # the console driver are not unwatched -- but the keystroke path is.
    #
    # RUN IT BY HAND from the test build directory:
    #
    #	cd build/kernel/test && besm6 ../../../kernel/test/console.ini
    #
    # not with `ctest -R console', which cannot run a disabled test -- though that command
    # is still worth giving, because console_setup is deliberately LEFT ENABLED and is what
    # refreshes the root3078.disk the line above attaches.  The script echoes its own
    # PASS/FAIL line either way.  Re-enable this as soon as the wobble is explained.
    #
    set_tests_properties(console PROPERTIES DISABLED TRUE)

    # session -- the end-to-end statement task 25b asks for: boot, shell, create and write
    # files, sync, fsck clean.  Three programs have to run in order (b6fsutil to build and
    # graft the disk, besm6 to hold the session, b6fsutil again to convert back, check and
    # extract), and the last step is a host-side diff, so this one is a SCRIPT rather than a
    # chain of fixtures: the sequence is the test, and a failure names its own stage.
    add_test(NAME session
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-session.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(session PROPERTIES LABELS kernel TIMEOUT 600 RESOURCE_LOCK simh_boot)

    # files -- the same statement one step further on, for tasks C1b and C1c: boot, shell, and
    # then cp, ln, mv and rm rearranging the tree that `session' could only create, followed by
    # chmod, chown, chgrp and touch changing what its files ARE.  Its own script and its own
    # volume (3082) rather than more lines in session.sh, because the question it asks is a
    # different one -- mv(1) renaming a directory into another parent is four calls, and a link
    # count it gets wrong is invisible to everything except the five-pass fsck run-files.sh
    # ends with.
    #
    # C1c added a SECOND host-side oracle beside that fsck, and it is the more reusable half:
    # `b6fsutil -v -v' sorted into files.modes, which is where a mode and an owner are
    # asserted.  They cannot be asserted in the guest -- `ls -l' prints a date and no time on
    # this machine is reproducible -- and the same will be true of every later task that
    # touches a timestamp.  C5 belongs here as it lands.
    add_test(NAME files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-files.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(files PROPERTIES LABELS kernel TIMEOUT 1800 RESOURCE_LOCK simh_boot)

    # utils -- task C2's small utilities, on its own volume (3083, and login owns 3084).  Its
    # own script again,
    # because the question is a third one: `session' asks whether files can be created,
    # `files' whether they can be rearranged, and this one asks about three things that are
    # not files at all -- the system clock (date, through stime(2)), an alarm that has to be
    # DELIVERED (sleep, through libc's alarm/pause/longjmp dance), and a signal sent from one
    # process to another (kill).  b6sim can assert none of the three, which is why the
    # cmd_date_*/cmd_sleep_*/cmd_kill_* cases beside the sources cover only argument handling.
    #
    # The oracle is a diff of what the programs PRINTED, with the seconds field masked by
    # run-utils.sh -- the same "keep only the reproducible columns" projection files uses over
    # `b6fsutil -v -v', and for the same reason.
    #
    # TASK C2b JOINED IT, which is what the second half of utils.sh is: tty(1) naming the
    # console (b6sim cannot read a directory, so it answers the opposite there), test(1)
    # branching through both its names -- `[' being the image's one hard link, which
    # rootimg_link checks from the host side -- yes(1) stopped by a SIGPIPE down THE FIRST
    # PIPELINE this image has ever run, and time(1) forking, execing and reading times(2),
    # with its three intervals masked like date's seconds and its exit status diffed as it
    # stands.  C5 belongs here too, as it lands.
    add_test(NAME utils
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-utils.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(utils PROPERTIES LABELS kernel TIMEOUT 1800 RESOURCE_LOCK simh_boot)

    # edit -- ed(1) ON A RUNNING SYSTEM, on its own volume (3086).  Task C3, and the question
    # is the one none of the tests above can ask: everything they assert was written on the
    # BUILD HOST and staged in by b6fsutil, and this is the test that says the guest no longer
    # needs it.  /etc/edit authors a file from nothing, edits two that cat wrote, and reads and
    # rewrites the one file on this image that v7's ed would have refused outright -- /etc/motd,
    # 365 bytes of UTF-8, which getfile()'s `c & 0200' test made a `?' and nothing else.
    #
    # The nineteen cmd_ed_* cases next door cover the command language and the regex engine
    # under b6sim, on the same staged bytes; what needs a kernel is a /tmp of this machine's
    # own, a `!' with a real /bin/sh to exec, files long enough to splice a line across two of
    # ed's 512-byte temp blocks against a real disk, and a filesystem to fsck afterwards.
    # run-edit.sh's two cmps are the strongest statement in it and need no checked-in fixture:
    # they compare what ed wrote against the original out of the same extracted image.
    add_test(NAME edit
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-edit.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(edit PROPERTIES LABELS kernel TIMEOUT 1800 RESOURCE_LOCK simh_boot)

    # fsinfo -- THE GUEST MEASURING ITS OWN FILESYSTEM, on its own volume (3087).  Task C4a,
    # and the question is one none of the tests above can ask: every one of them asks what
    # the guest can DO to the filesystem and then asks the HOST whether the result is still
    # one.  This asks whether the guest can say anything about the store it is living on --
    # df(1) counting the free list off the raw device, du(1) walking a tree, quot(1)
    # sweeping the whole i-list -- and nothing before it could.
    #
    # What needs a kernel here is the RAW DEVICE PATH.  The cmd_df_* and cmd_quot_* cases
    # next door run the same bytes against a fixture filesystem under b6sim in a tenth of a
    # second, but b6sim's read(2) is the host's, so none of the four alignment rules
    # physio() and mdstrategy() impose exists there -- cmd/df/README.md is the account.  du
    # cannot run there at all, reading directories as ls(1) does.
    #
    # TWO OF ITS FOUR ORACLES RECOMPUTE RATHER THAN REMEMBER, which is the reusable half of
    # this task and the answer to a problem run-files.sh's checked-in files.modes has: df's
    # and quot's numbers are about the WHOLE image and move whenever a program joins /bin.
    # So they are not checked in -- the host works them out again from the finished image,
    # `b6fsutil -c -v' for the free count and an awk over `b6fsutil -v -v' for the per-uid
    # sums.  It also captures the CONSOLE, which no test here did before, because a program
    # that measures the filesystem cannot write its answer into it without changing it.
    add_test(NAME fsinfo
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-fsinfo.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(fsinfo PROPERTIES LABELS kernel TIMEOUT 1800 RESOURCE_LOCK simh_boot)

    # login -- MULTI-USER, on its own volume (3084).  Task 29b, and the question is a fifth
    # one: everything above types at the single-user shell init hands out, and this one types
    # at a getty.  /etc/ttys, /etc/getty and /bin/login, crypt(3) against /etc/passwd, the
    # /etc/utmp nothing had ever written, init's multiple() respawn loop -- which no other test
    # in this suite can reach, having no child of init to put in it -- and a shell belonging to
    # uid 7, which is the first non-root shell this port has ever produced.
    #
    # A SCRIPT rather than a bare .ini, for run-session.sh's reason: the dialogue proves what
    # the operator sees and the host has to be asked about the rest.  /etc/utmp exists only
    # because merge() made it, and /dev/console ends up owned by guest only because login's
    # chown() ran before it dropped root -- neither is visible from the console, and the second
    # is the only assertion in the tree that login's one privileged call happened at all.
    #
    # It also configures BOTH Consuls -- a telnet listener on 4199 for the second, so that
    # /dev/tty1 is a terminal a HUMAN can reach rather than a discarded line.  Only Consul 1 is
    # driven here; the two-terminal dialogue is `multi' below.  The fixed port is safe only
    # because of the RESOURCE_LOCK.
    add_test(NAME login
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-login.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(login PROPERTIES LABELS kernel TIMEOUT 600 RESOURCE_LOCK simh_boot)

    # ---------------------------------------------------------------------------------
    # multi -- TWO PEOPLE LOGGED IN AT ONCE, on the two Consul typewriters, on its own volume
    # (3085).  Task 29c, and what it adds to login is the second terminal: root on
    # /dev/console and guest on /dev/tty1, each shell naming its own line, guest's file read by
    # root's shell, and NEITHER LOGGING OUT -- so the disk afterwards carries two different
    # non-zero owners on two terminal nodes at the same instant, which no single session can
    # produce however it is driven.
    #
    # IT NEEDS A HOST PROGRAM, and it is the only test here that does.  Consul 1 is bound to the
    # simulator's own stdin/stdout by `attach tty25 console'; Consul 2 is a mux line, and
    # besm6_tty.c's vt_puts() discards everything printed to it while nothing is connected.
    # ttyhost binds a port, the simulator DIALS OUT to it as multi.ini is parsed -- before `go',
    # which is what removes the race against the once-a-second connection poll -- and it writes
    # down what Consul 2 printed.  It types nothing: multi.ini drives both lines, with
    # `send TTY:26,' for the second.  ttyhost.c's header is the account.
    #
    # `ttyhost' is a HOST executable, the one in this directory: everything else here is BESM-6
    # code built by the b6* toolchain.  It is not installed.
    add_executable(ttyhost ${CMAKE_CURRENT_SOURCE_DIR}/ttyhost.c)
    list(APPEND TEST_TARGETS ttyhost)

    add_test(NAME multi
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-multi.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE:ttyhost>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(multi PROPERTIES LABELS kernel TIMEOUT 600 RESOURCE_LOCK simh_boot)

    # ---------------------------------------------------------------------------------
    # swap -- more processes than core, and two of them sharing one binary's text
    # (task 26).
    # ---------------------------------------------------------------------------------
    #
    # The same script shape as `session', for the same reason, and one thing more: its .ini
    # is GENERATED, because it deposits a smaller `phymem' before the boot and asserts on
    # three kernel counters afterwards, and all four are link-time addresses.  So swap.ini
    # comes out of the build directory while session.ini and console.ini are used from the
    # source directory as they stand.  swap.ini.in says why each of the four is there.
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/swap.ini
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
                "-DSYMS=phymem;nswapout;nswapin;ntextjoin"
                -DIN=${CMAKE_CURRENT_SOURCE_DIR}/swap.ini.in
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/swap.ini
                -P ${CMAKE_CURRENT_SOURCE_DIR}/genboot.cmake
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
                ${CMAKE_CURRENT_SOURCE_DIR}/swap.ini.in
                ${CMAKE_CURRENT_SOURCE_DIR}/genboot.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "gen swap.ini" VERBATIM)
    add_custom_target(swapini DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/swap.ini)
    add_dependencies(swapini kernel rootimg)
    list(APPEND TEST_TARGETS swapini)

    add_test(NAME swap
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-swap.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(swap PROPERTIES LABELS kernel TIMEOUT 900 RESOURCE_LOCK simh_boot)

    # ---------------------------------------------------------------------------------
    # libtest -- the twenty-six /usr/test programs, run off the image by the booted kernel
    # (task 25c).
    # ---------------------------------------------------------------------------------
    #
    # One boot produces all the output and one ctest case adjudicates each program, which
    # is why this is a FIXTURE and not a script like `session': there twenty-six diffs
    # would collapse into a single red case, and a divergence in `timet' would be
    # indistinguishable from a kernel that never reached the prompt.  So run-libtest.sh
    # stops once it has extracted the disk, and everything downstream of it is its own
    # case, requiring the fixture.
    #
    # WHICH EXPECTATION EACH CASE USES.  lib/test/<name>.expected -- the very file b6sim is
    # held to -- unless kernel/test/libtest/<name>.expected exists, in which case that one
    # wins.  The default is deliberate: an unchanged expectation is a POSITIVE result, the
    # statement that the kernel and the simulator answered a real program identically.  An
    # override file's existence is the statement that they cannot, and its header says why.
    #
    # Its .ini is GENERATED, like swap's and for the same reason: it asserts that iomove()'s
    # bulk copy path was reached (niobulk non-zero) and prints the other two counters, and all
    # three are link-time addresses.  See libtest.ini.in.
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libtest.ini
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
                "-DSYMS=niobulk;nioedge;nioshift"
                -DIN=${CMAKE_CURRENT_SOURCE_DIR}/libtest.ini.in
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/libtest.ini
                -P ${CMAKE_CURRENT_SOURCE_DIR}/genboot.cmake
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
                ${CMAKE_CURRENT_SOURCE_DIR}/libtest.ini.in
                ${CMAKE_CURRENT_SOURCE_DIR}/genboot.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "gen libtest.ini" VERBATIM)
    add_custom_target(libtestini DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libtest.ini)
    add_dependencies(libtestini kernel rootimg)
    list(APPEND TEST_TARGETS libtestini)

    add_test(NAME libtest_run
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-libtest.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(libtest_run PROPERTIES FIXTURES_SETUP libtest
                         LABELS kernel TIMEOUT 1200 RESOURCE_LOCK simh_boot)

    # The structural half, as in run-session.sh: forty-two execs and twenty-odd files
    # written, and the filesystem still checks out.
    add_test(NAME libtest_fsck COMMAND ${B6FSUTIL} -c libafter.img
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(libtest_fsck PROPERTIES FIXTURES_REQUIRED libtest LABELS kernel)

    foreach(_p ${B6_LIBTEST_IMAGE})
        set(_exp ${CMAKE_CURRENT_SOURCE_DIR}/libtest/${_p}.expected)
        if(NOT EXISTS ${_exp})
            set(_exp ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/test/${_p}.expected)
        endif()
        add_test(NAME libtest_${_p}
                 COMMAND diff -u ${_exp} libtest.out/tmp/${_p}.out
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(libtest_${_p} PROPERTIES FIXTURES_REQUIRED libtest
                             LABELS kernel)
    endforeach()
endif()

# `make test' truncates mdshort3076.disk to nothing before running mdtest: run 4 attaches
# it WITHOUT -n so it stays empty, the only way to make the simulator produce a soft error
# (a read off the end of the file) and so the only exercise the driver's retry path gets.
# A ctest setup fixture recreates it empty in the binary dir before every mdtest run.
if(BESM6)
    add_test(NAME mdtest_setup COMMAND sh -c ": > mdshort3076.disk"
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(mdtest_setup PROPERTIES FIXTURES_SETUP md_disk LABELS kernel)
    set_tests_properties(mdtest       PROPERTIES FIXTURES_REQUIRED md_disk)
endif()

# Aggregate target the wrapper builds before running ctest (ctest itself never builds).
add_custom_target(tests DEPENDS ${TEST_TARGETS})

# When built inside the top-level project, hook the images into its build_tests aggregate,
# so the top-level `make test' (which builds build_tests) and `make run' (ctest) cover the
# kernel SIMH tests too.
if(TARGET build_tests)
    add_dependencies(build_tests ${TEST_TARGETS})
endif()
