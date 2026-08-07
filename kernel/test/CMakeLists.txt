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

# The tests that BOOTED the whole kernel and drove it by typing at the guest are gone --
# eighteen dialogues and the image-side half of the libc suite, with the `weekly' label
# and `make weekly' that selected them.  Two are left, both at the foot of this file:
# `boot', a one-second smoke test that the kernel still reaches a shell prompt, and
# `multi', which carries on from there into multi-user mode with two people logged in.

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
# is the same graft the deleted run-*.sh drivers used to get a script onto their own copy.
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
    ${B6_ROOTFS}/etc/clri
    ${B6_ROOTFS}/etc/dcheck
    ${B6_ROOTFS}/etc/fsck
    ${B6_ROOTFS}/etc/getty
    ${B6_ROOTFS}/etc/icheck
    ${B6_ROOTFS}/etc/iostat
    ${B6_ROOTFS}/etc/mkfs
    ${B6_ROOTFS}/etc/mount
    ${B6_ROOTFS}/etc/ncheck
    ${B6_ROOTFS}/etc/pstat
    ${B6_ROOTFS}/etc/quot
    ${B6_ROOTFS}/etc/umount
    ${B6_ROOTFS}/etc/wall
    ${B6_ROOTFS}/etc/group
    ${B6_ROOTFS}/etc/motd
    ${B6_ROOTFS}/etc/passwd
    ${B6_ROOTFS}/etc/rc
    ${B6_ROOTFS}/etc/termcap
    ${B6_ROOTFS}/etc/ttys
    ${B6_ROOTFS}/usr/dict/words
    ${B6_ROOTFS}/bin/sh
    ${B6_ROOTFS}/bin/basename
    ${B6_ROOTFS}/bin/cal
    ${B6_ROOTFS}/bin/cat
    ${B6_ROOTFS}/bin/chgrp
    ${B6_ROOTFS}/bin/chmod
    ${B6_ROOTFS}/bin/chown
    ${B6_ROOTFS}/bin/cmp
    ${B6_ROOTFS}/bin/col
    ${B6_ROOTFS}/bin/comm
    ${B6_ROOTFS}/bin/cp
    ${B6_ROOTFS}/bin/date
    ${B6_ROOTFS}/bin/dd
    ${B6_ROOTFS}/bin/df
    ${B6_ROOTFS}/bin/diff
    ${B6_ROOTFS}/bin/dmesg
    ${B6_ROOTFS}/bin/du
    ${B6_ROOTFS}/usr/lib/diffh
    ${B6_ROOTFS}/usr/bin/cpp
    ${B6_ROOTFS}/usr/bin/as
    ${B6_ROOTFS}/usr/bin/ld
    ${B6_ROOTFS}/usr/bin/nm
    ${B6_ROOTFS}/usr/bin/size
    ${B6_ROOTFS}/usr/bin/strip
    ${B6_ROOTFS}/usr/bin/disasm
    ${B6_ROOTFS}/usr/bin/lorder
    ${B6_ROOTFS}/usr/bin/ar
    ${B6_ROOTFS}/usr/bin/ranlib
    ${B6_ROOTFS}/usr/bin/cc
    ${B6_ROOTFS}/usr/bin/man
    ${B6_ROOTFS}/usr/bin/manview
    ${B6_ROOTFS}/usr/guest/hello.S
    ${B6_ROOTFS}/bin/echo
    ${B6_ROOTFS}/bin/ed
    ${B6_ROOTFS}/bin/fgrep
    ${B6_ROOTFS}/bin/file
    ${B6_ROOTFS}/bin/find
    ${B6_ROOTFS}/bin/grep
    ${B6_ROOTFS}/bin/join
    ${B6_ROOTFS}/bin/kill
    ${B6_ROOTFS}/bin/ln
    ${B6_ROOTFS}/bin/login
    ${B6_ROOTFS}/bin/look
    ${B6_ROOTFS}/bin/ls
    ${B6_ROOTFS}/bin/mesg
    ${B6_ROOTFS}/bin/mkdir
    # No entry for /bin/less: it is a hard link to this one and stages no file.
    ${B6_ROOTFS}/bin/more
    ${B6_ROOTFS}/bin/mv
    ${B6_ROOTFS}/bin/newgrp
    ${B6_ROOTFS}/bin/nice
    ${B6_ROOTFS}/bin/novi
    ${B6_ROOTFS}/bin/od
    ${B6_ROOTFS}/bin/passwd
    ${B6_ROOTFS}/bin/pr
    ${B6_ROOTFS}/bin/ps
    ${B6_ROOTFS}/bin/pwd
    ${B6_ROOTFS}/bin/rev
    ${B6_ROOTFS}/bin/rm
    ${B6_ROOTFS}/bin/rmdir
    ${B6_ROOTFS}/bin/sed
    ${B6_ROOTFS}/bin/sleep
    ${B6_ROOTFS}/bin/sort
    ${B6_ROOTFS}/bin/split
    ${B6_ROOTFS}/bin/stty
    ${B6_ROOTFS}/bin/su
    ${B6_ROOTFS}/bin/sum
    ${B6_ROOTFS}/bin/sync
    ${B6_ROOTFS}/bin/tail
    ${B6_ROOTFS}/bin/tar
    ${B6_ROOTFS}/bin/tee
    ${B6_ROOTFS}/bin/test
    ${B6_ROOTFS}/bin/time
    ${B6_ROOTFS}/bin/touch
    ${B6_ROOTFS}/bin/tr
    ${B6_ROOTFS}/bin/tsort
    ${B6_ROOTFS}/bin/tty
    ${B6_ROOTFS}/bin/uniq
    ${B6_ROOTFS}/bin/vmstat
    ${B6_ROOTFS}/bin/wc
    ${B6_ROOTFS}/bin/who
    ${B6_ROOTFS}/bin/write
    ${B6_ROOTFS}/bin/yes)
# /bin/[ is deliberately absent: it is a `link' stanza in the manifest, not a staged file,
# so there is nothing here for the image to depend on.  rootimg_link below is what holds it.

# /usr/test IS NOT HERE ANY MORE.  The lib/test programs were on this image until the test
# pack existed and are now on that one -- see TESTFS_FILES below, and ../../test.manifest for
# why they moved.  Nothing else on the root refers to them.

# ...and /lib and /usr/include, the development tree task C9e put on the image so that
# /usr/bin/cc's default link line has something to find.  The three lists are the
# top-level CMakeLists.txt's, declared there before this directory is added, for exactly
# the reason above: one list, so the staging and this dependency cannot drift.  A
# standalone `cmake -S kernel' has none of them and builds no image either.
foreach(_f ${B6_STAGE_LIB})
    list(APPEND ROOTFS_FILES ${B6_ROOTFS}/lib/${_f})
endforeach()
foreach(_f ${B6_STAGE_INC} ${B6_STAGE_FREEINC})
    list(APPEND ROOTFS_FILES ${B6_ROOTFS}/usr/include/${_f})
endforeach()

# ...and /usr/man, the two hundred manual pages.  B6_STAGE_MAN is the same kind of list and is
# declared in the same place; it holds image-relative names -- man1/ls.1, man3/stdio.3s -- so
# this is the whole of what the manual costs here.
foreach(_f ${B6_STAGE_MAN})
    list(APPEND ROOTFS_FILES ${B6_ROOTFS}/usr/man/${_f})
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

# ---------------------------------------------------------------------------------------
# THE TEST PACK -- the second filesystem, and the second drive's first standing job.
# ---------------------------------------------------------------------------------------
#
# test.img holds the lib/test programs as /test/*, and a booted kernel reaches them as
# /mnt/test/* after `/etc/mount /dev/md1 /mnt -r'.  They were /usr/test on root.img until
# this existed; ../../test.manifest's header is the whole account, including why nothing
# mounts it automatically.
#
# ONE SHARED READ-ONLY ARTIFACT.  Every .ini that wants it does `attach -r md01
# test3077.disk' and mounts it -r, so unlike root.img -- which each booting test copies at a
# volume of its own, because it gets written -- there is one copy and one volume number for
# all of them.  Volume 3077: 3072 is the root artifact, mdtest owns 3073-3076, and this is
# the next free number.
#
# SAME -T STAMP AS THE ROOT, so the pack is byte-reproducible too.  It is not the boot clock
# -- iinit() seeds `time' from the ROOT superblock's s_time and never looks at a mounted
# volume's -- so the value matters here only for reproducibility.
#
# The build-ORDER edge (testfs must be staged before b6fsutil runs) is drawn at the foot of
# the top-level CMakeLists.txt, beside rootimg's and for the same reason.
get_filename_component(TEST_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/../../test.manifest ABSOLUTE)

# The list is lib/test's, since that is where these are linked and staged; naming it from one
# file is what keeps the manifest and this dependency list from drifting apart.  Read by a
# path relative to this directory so that a standalone `cmake -S kernel' still configures --
# it cannot build either image anyway, for the reason the manifests' headers give.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/test/progs.cmake)
set(TESTFS_FILES "")
foreach(_p ${B6_LIBTEST_IMAGE})
    list(APPEND TESTFS_FILES ${B6_TESTFS}/test/${_p})
endforeach()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test.img
    COMMAND ${B6FSUTIL} -n -s 2000 -T ${ROOTTIME}
            -M ${TEST_MANIFEST} test.img
    DEPENDS ${TEST_MANIFEST} ${TESTFS_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil test.img" VERBATIM)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test3077.disk
    COMMAND ${B6FSUTIL} -S test.img test3077.disk
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test.img
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil -S test3077.disk" VERBATIM)

add_custom_target(testimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test3077.disk)
list(APPEND TEST_TARGETS testimg)

# The image is self-consistent before any kernel touches it.  Cheap, and it keeps a
# b6fsutil regression from arriving disguised as a driver bug in fstest.  Not guarded by
# BESM6: it is a host check on a host tool's output, and runs whether or not the simulator
# is installed.
add_test(NAME rootimg_check COMMAND ${B6FSUTIL} -c root.img
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rootimg_check PROPERTIES LABELS kernel)

# The same check on the test pack, and it is the ONLY thing that ever inspects it: the pack is
# mounted read-only and no guest writes it, so a `-c' failure here can only mean b6fsutil or
# ../../test.manifest.  That is worth having anyway -- kernel/test/core mounts this image, and
# a filesystem sbcheck() refuses would fail that test with a message about mount(1).
add_test(NAME testimg_check COMMAND ${B6FSUTIL} -c test.img
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(testimg_check PROPERTIES LABELS kernel)

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
                   ${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/rmdir$' && \
                   ${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/newgrp$' && \
                   ${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/passwd$' && \
                   ${B6FSUTIL} -v -v root.img | grep -q '^- 4755 0/0 .* /bin/su$'"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rootimg_setuid PROPERTIES LABELS kernel)

# THE HARD LINKS, READ BACK OFF THE FINISHED IMAGE (tasks C2b and C27).  /bin/[ is /bin/test
# and /bin/less is /bin/more -- each the same inode under two names -- and they are the only
# two entries in ../../root.manifest that name no `source'.  Nothing in build/rootfs/
# corresponds to either, so both are invisible to ROOTFS_FILES above and to every size check;
# the image is again the only thing that can say whether they are there.
#
# WHY IT MATTERS RATHER THAN BEING DECORATION: cmd/test/test.c looks at argv[0] and requires a
# closing `]' when it was entered by that name, so with no link that branch is unreachable and
# `[ -f x ]' in a shell script is a command not found.  Nothing types it now: kernel/test/utils.sh
# did, and went with the rest of the tests that booted.
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

# ... and the same two assertions for /bin/less -> /bin/more (task C27).  A SECOND TEST rather
# than a widened first one, because what the two links are for is not the same thing: the one
# above makes a branch of cmd/test reachable at all, while this one is a convenience -- nothing
# in cmd/more/more.c reads argv[0] -- and the comment above should go on being true of its own
# link.  A failure here means the manifest stanza or b6fsutil's link resolution, not more(1).
add_test(NAME rootimg_link_more
    COMMAND sh -c "${B6FSUTIL} -v -v root.img | grep -E ' /bin/(more|less)$' | grep -c 'nlink 2 ' | grep -qx 2 && \
                   ${B6FSUTIL} -v -v root.img | grep -E ' /bin/(more|less)$' | sed 's| /bin/.*||' | sort -u | grep -c . | grep -qx 1"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rootimg_link_more PROPERTIES LABELS kernel)

# ---------------------------------------------------------------------------------------
# boot -- the whole kernel booting with the root image attached (task 20).
# ---------------------------------------------------------------------------------------
#
# It links nothing of its own: it loads the real `unix' image built one directory up by
# kernel/CMakeLists.txt, against the filesystem fstest reads.  Process 1 leaves the kernel,
# execs /etc/init, init forks /bin/sh and the shell PROMPTS -- rather than panicking (no
# root fs), falling into the icode's failure loop, stopping anywhere in the kernel or
# hanging.  Still the regression guard for the b_un.b_addr fat-pointer fix and for the
# whole path into user mode: console, session and the image-side libtest went with the rest
# of the weekly half.  `multi' below is the other test that boots, and carries on where this
# one stops.
#
# `boot' ATTACHES root3072.disk READ-ONLY, which is an assertion in itself: the boot path
# needs no writes.
#
# RUN_SERIAL, HERE AND ON `multi', AND NOT A RESOURCE_LOCK.  A lock between the two would only
# keep them apart from EACH OTHER; these two are sensitive to the load of the whole machine,
# not just to one another.  SIMH drops characters out of a `send' when the host is
# oversubscribed, and both run on a step budget that a loaded box can exhaust before the guest
# has got there -- so a green run says nothing if 600 GoogleTest cases were competing for the
# cores.  RUN_SERIAL is ctest's "start nothing else while this runs", which is the property
# that actually matches the requirement.  It costs a few seconds of total wall clock, since
# neither can overlap anything; that is the price of a boot dialogue meaning what it says.
# ../TODO.md task 35 has the two bugs that were found underneath it.
#
# `spin' is a LINKED address that moves as the kernel grows, so boot.ini is
# GENERATED from unix.nm at build time (genboot.cmake) rather than hardcoded.  The generation
# depends on the `kernel' target so the image and its .nm exist first, and the test needs
# root.img/root3072.disk (rootimg) in its cwd -- both pulled in below.
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
    set_tests_properties(boot PROPERTIES LABELS kernel RUN_SERIAL TRUE)

    # ---------------------------------------------------------------------------------
    # multi -- TWO PEOPLE LOGGED IN AT ONCE, on the two Consul typewriters, on its own volume
    # (3085).  Task 29c.  `boot' stops at the single-user prompt; this one types ^D and goes
    # the rest of the way -- /etc/rc, a getty per line of /etc/ttys, crypt(3) against
    # /etc/passwd, login dropping privilege -- and then puts root on /dev/console and guest
    # on /dev/tty1, each shell naming its own line, guest's file read by root's shell, and
    # NEITHER LOGGING OUT.  So the disk afterwards carries two different non-zero owners on
    # two terminal nodes at the same instant, which no single session can produce however it
    # is driven.
    #
    # IT NEEDS A HOST PROGRAM, and it is the only test here that does.  Consul 1 is bound to
    # the simulator's own stdin/stdout by `attach tty25 console'; Consul 2 is a mux line, and
    # besm6_tty.c's vt_puts() discards everything printed to it while nothing is connected.
    # ttyhost binds a port, the simulator DIALS OUT to it as multi.ini is parsed -- before
    # `go', which is what removes the race against the once-a-second connection poll -- and it
    # writes down what Consul 2 printed.  It types nothing: multi.ini drives both lines, with
    # `send TTY:26,' for the second.  ttyhost.c's header is the account.
    #
    # `ttyhost' is a HOST executable, the one in this directory: everything else here is
    # BESM-6 code built by the b6* toolchain.  It is not installed.
    #
    # No fixture: run-multi.sh converts its own writable copy of root.img, which TEST_TARGETS
    # already builds, and the `kernel' target supplies the ../unix it loads.
    add_executable(ttyhost ${CMAKE_CURRENT_SOURCE_DIR}/ttyhost.c)
    list(APPEND TEST_TARGETS ttyhost)

    add_test(NAME multi
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-multi.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE:ttyhost>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(multi PROPERTIES LABELS kernel TIMEOUT 600 RUN_SERIAL TRUE)

    # ---------------------------------------------------------------------------------
    # core -- ONE USER PROGRAM RUN BY THE REAL KERNEL, on its own volume (3086).  The
    # check on kernel task 34, the `int'-vs-pointer sweep.
    # ---------------------------------------------------------------------------------
    #
    # `boot' stops at the single-user prompt; this types two commands at it -- a mount of
    # the test pack, then /mnt/test/coret -- and lets the program adjudicate itself on the
    # console.  What it is about is an address the kernel FABRICATED OUT OF AN INTEGER:
    # core() dumping the user's image through `u_base = 0' -- a bit copy whose byte field
    # reads as #5, so the whole image went to disk five bytes out of phase with a chunk
    # missing off the end -- and ptrace(2)'s addr, whose unit was written down nowhere.
    #
    # THE THIRD TEST THAT BOOTS, and the only one that runs a user program at all since the
    # image-side libtest was deleted.  It exists as a test of its own rather than as a
    # libtest case because NEITHER ARM RUNS UNDER b6sim: there is no core dump there at
    # all, and ptrace is refused with EPERM.  The two bugs the sweep started from were
    # found by a user program under a booted kernel and by nothing else, which is the
    # whole argument for this file.
    #
    # IT IS ALSO THE ONLY LIVE CALLER OF smount(), since kernel/test/mount went with the
    # rest of the tests that booted: cmd/mount's own b6sim cases can reach the argument
    # diagnostics and nothing further.  A failure at the mount rather than at a verdict is
    # therefore a mount(2)/mount(1) failure, not a coret one; core.ini's echof says so.
    #
    # RUN_SERIAL for the reason `boot' and `multi' carry it: these type at the guest on a
    # step budget, and SIMH drops characters out of a `send' when the host is
    # oversubscribed.  It is not a lock -- what they are sensitive to is the load of the
    # whole machine, not one another.
    #
    # No fixture and no generated .ini: run-core.sh converts its own writable copy of
    # root.img and attaches the shared read-only test3077.disk, both of which TEST_TARGETS
    # already builds, and nothing here is a linked address.
    add_test(NAME core
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-core.sh
                ${B6FSUTIL} ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(core PROPERTIES LABELS kernel TIMEOUT 600 RUN_SERIAL TRUE)

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
