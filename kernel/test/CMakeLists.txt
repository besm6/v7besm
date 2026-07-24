# Standalone BESM-6 programs, run on the SIMH simulator (`besm6').
#
# Not host unit tests: each is a BESM-6 program that links kernel objects against a
# hand-built environment, plus a .ini that loads it into the simulator, runs it, and
# asserts on the machine state (each .ini `exit 1's on failure, so a nonzero exit fails
# the ctest case).  Reuses b6_obj/b6_find_src from the parent CMakeLists.
#
# Kernel objects are compiled HERE from the sources next door (never borrowed from
# kernel/'s own build), found by b6_find_src searching `. .. ../dev'.  Only the kernel
# .c sources get -DKERNEL (KERNOBJ); the local test programs and the .S/.s assist must
# not, matching the old Makefile's static-pattern rule.

set(KERNOBJ alloc bio md mb slp malloc syscall sendsig clock intr utab)

set(TEST_TARGETS "")

# Compile ${base}.o for program ${prog} into its OWN directory (${prog}.dir), setting -DKERNEL
# only for the kernel .c sources.  Objects are per-program on purpose: many images share a
# source (utab, brz, crt0, ...), and a single shared object OUTPUT would be emitted into every
# consuming target's build rules and race under `make -j' (MAKEFLAGS=-j8), corrupting the file
# a link then reads.  Recompiling a shared source once per image trades a little build time for
# a parallel-safe graph.  Sets ${outvar} to the object path.
function(b6_test_obj outvar prog base)
    b6_find_src(_src ${base})
    set(B6_OBJDIR ${CMAKE_CURRENT_BINARY_DIR}/${prog}.dir)
    if("${base}" IN_LIST KERNOBJ)
        b6_obj(_o ${_src} -DKERNEL)
    else()
        b6_obj(_o ${_src})
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
b6_test(uintr    crt0u uintr utab brz)
b6_test(utrap    crt0t utrap utab brz)
b6_test(usys     crt0s usys syscall utab brz usermem)
b6_test(usig     crt0sg usig syscall sendsig utab brz usermem)
b6_test(uclock   crt0c uclock clock intr utab brz)
b6_test(uswtch   crt0w uswtch switch uarea brz intr)
b6_test(usched   crt0w usched slp switch uarea brz intr utab seg malloc)
b6_test(ugrow    crt0g ugrow utab brz seg)
b6_test(biotest  crt0 biotest bio utab brz)
b6_test(ugrp     crt0 ugrp intr)
b6_test(mbtest   crt0 mbtest mb intr)
b6_test(mdtest   crt0 mdtest md intr)
b6_test(fstest   crt0 fstest bio md alloc intr utab brz)

# ---------------------------------------------------------------------------------------
# The root filesystem image fstest reads (task 19).
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
set(ROOTTIME 1000000000)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/root.img
    COMMAND ${B6FSUTIL} -n -s 2000 -T ${ROOTTIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/root.manifest root.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/root.manifest
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

# ---------------------------------------------------------------------------------------
# boot -- the whole kernel booting with the root image attached (task 20).
# ---------------------------------------------------------------------------------------
#
# Links nothing of its own: it loads the real `unix' image built one directory up by
# kernel/CMakeLists.txt, attaches the same root3072.disk fstest reads, and asserts that process 1
# leaves the kernel and parks in the icode's failure loop (`spin') with ENOENT for the missing
# /etc/init -- rather than panicking (no root fs), stopping anywhere in the kernel or hanging.
# Still the regression guard for the b_un.b_addr fat-pointer fix, and now for the whole path into
# user mode.  See boot.ini.in for the full story, including what it does NOT check.
#
# `spin' is a LINKED address that moves as the kernel grows, so boot.ini is
# GENERATED from unix.nm at build time (genboot.cmake) rather than hardcoded.  The generation
# depends on the `kernel' target so the image and its .nm exist first, and the `boot' test
# needs root3072.disk (rootimg) in its cwd -- both are pulled in below.
if(BESM6)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot.ini
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_CURRENT_BINARY_DIR}/../unix.nm
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
    set_tests_properties(boot PROPERTIES LABELS kernel)
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
