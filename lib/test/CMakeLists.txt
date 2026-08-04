# Test programs for the user-level libraries, run under b6sim AND on the disk image.
#
# b6sim runs one user a.out and services the v7 syscalls on the host, far faster than booting
# SIMH.  Each program is linked against the real crt0.o and libc.a, run, and its output diffed
# against the checked-in .expected file (see run-test.sh); a program that needs arguments lists
# them in a matching .args file.
#
# SINCE TASK 25c THE SAME IMAGES ALSO GO ON THE ROOT FILESYSTEM, as /usr/test/<name>, and the
# booted kernel runs them from there (kernel/test/libtest.{sh,ini}, run-libtest.sh).  That is
# where sysent[]'s rows meet a real caller: under b6sim every syscall is the host's, and a
# kernel bug cannot show.  The two runs share ONE LINKED IMAGE -- staged by a copy, not by a
# second link -- so a difference between them can only be the harness, never the compiler.
#
# The two halves used to belong to two helpers that each did one of them: b6_prog() in
# scripts/BesmCross.cmake stages and size-checks but never runs, b6_libtest() ran but never
# size-checked.  b6_libtest() now does both, which also sidesteps b6_prog()'s one gap -- it
# cannot link -lm, and matht needs it.
#
# progs.cmake carries the list of what goes on the image, because kernel/test/CMakeLists.txt
# needs the same list and is configured in another directory.

# The sibling libraries' build outputs -- crt0.o and libc.a next door in ../libc, libm.a in
# ../libm, libtermcap.a in ../libtermcap.  Normalized so the `..' does not reach the b6ld
# command line.
get_filename_component(LIBC_DIR ${CMAKE_CURRENT_BINARY_DIR}/../libc ABSOLUTE)
get_filename_component(LIBM_DIR ${CMAKE_CURRENT_BINARY_DIR}/../libm ABSOLUTE)
get_filename_component(LIBTC_DIR ${CMAKE_CURRENT_BINARY_DIR}/../libtermcap ABSOLUTE)
get_filename_component(LIBCU_DIR ${CMAKE_CURRENT_BINARY_DIR}/../libcurses ABSOLUTE)
set(CRT0    ${LIBC_DIR}/crt0.o)
set(LIBC_A  ${LIBC_DIR}/libc.a)
set(LIBM_A  ${LIBM_DIR}/libm.a)
set(LIBTC_A ${LIBTC_DIR}/libtermcap.a)
set(LIBCU_A ${LIBCU_DIR}/libcurses.a)

set(TEST_TARGETS "")

include(${CMAKE_CURRENT_SOURCE_DIR}/progs.cmake)

# Link one test program: crt0.o first, its own object, then the libraries.  Link order is the
# contract -- [-lm] [-lcurses] [-ltermcap] then -lc then -lruntime, b6ld scanning each archive
# once (libm, libcurses and libtermcap call into libc, libc into libruntime, never back).  Pass
# `libm', `libcurses' or `libtermcap' in ARGN for the programs that need one.  libcurses comes
# ahead of libtermcap because curses is what calls tgetent/tgetstr/tgoto/tputs and termcap calls
# nothing back; a program that names libcurses must name libtermcap too.
# add_dependencies pins the sibling libraries ahead of the link across directory scopes.
#
# Then, unless the program is one of the two that belongs to a single world, STAGE the linked
# image into ${B6_ROOTFS}/usr/test/ for the disk picture, and register the size check that
# every program on that image gets.  ARGN also takes:
#
#   SIMONLY    b6sim test, no staging   -- spawn, whose premise is that /bin/sh cannot be exec'd
#   IMAGEONLY  staging, no b6sim test   -- shellt, which needs a shell that really starts,
#              and memt, which reads and writes the kernel's own memory through /dev/mem
#   PURE       link -n (NMAGIC)         -- puret, the one program that is about shared text
#
# THE STAGED FILE IS A COPY OF THE LINKED ONE, not a second link.  Two links of one source
# could drift (b6_prog() and this macro do not pass identical flags), and then a divergence
# between the b6sim run and the kernel run would have a third possible cause.  A copy makes
# `the same bytes ran in both places' a fact rather than an assumption.  It also keeps the
# .nm/.dis listings and the scratch files each run leaves behind out of ${B6_ROOTFS}, which
# becomes a disk image and may hold nothing but what belongs on it.
macro(b6_libtest prog)
    b6_obj(_o ${CMAKE_CURRENT_SOURCE_DIR}/${prog}.c)
    set(_mlib "")
    set(_extra_dep "")
    set(_extra_lib "")
    # ARGN is not a real variable inside a macro, so copy it before IN_LIST can see it.
    set(_argn ${ARGN})
    if("libm" IN_LIST _argn)
        set(_mlib -L${LIBM_DIR} -lm)
        set(_extra_dep ${LIBM_A})
        set(_extra_lib libm)
    endif()
    # Before the libtermcap arm, so that -lcurses precedes -ltermcap on the command line.
    if("libcurses" IN_LIST _argn)
        list(APPEND _mlib -L${LIBCU_DIR} -lcurses)
        list(APPEND _extra_dep ${LIBCU_A})
        list(APPEND _extra_lib libcurses)
    endif()
    if("libtermcap" IN_LIST _argn)
        list(APPEND _mlib -L${LIBTC_DIR} -ltermcap)
        list(APPEND _extra_dep ${LIBTC_A})
        list(APPEND _extra_lib libtermcap)
    endif()
    # PURE: see b6_prog() in scripts/BesmCross.cmake for what NMAGIC means to this kernel.
    set(_pure "")
    if("PURE" IN_LIST _argn)
        set(_pure -n)
    endif()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${prog}
        COMMAND ${B6LD} ${_pure} ${CRT0} ${_o} ${_mlib} -o ${prog} -L${LIBC_DIR} -L${B6LIBDIR} -lc -lruntime
        COMMAND sh -c "${B6NM} -n ${prog} > ${prog}.nm"
        COMMAND sh -c "${B6DISASM} -c ${prog} > ${prog}.dis"
        DEPENDS ${_o} ${CRT0} ${LIBC_A} ${_extra_dep}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "b6ld ${prog}" VERBATIM)
    # lib_ prefix on the target and ctest names: the kernel test dir has its own `hello', and
    # target/test names share one global namespace across the project.
    add_custom_target(lib_${prog}_img DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${prog})
    add_dependencies(lib_${prog}_img libc ${_extra_lib})
    list(APPEND TEST_TARGETS lib_${prog}_img)
    if(B6SIM AND NOT "IMAGEONLY" IN_LIST _argn)
        add_test(NAME lib_${prog}
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-test.sh ${B6SIM} ${CMAKE_CURRENT_SOURCE_DIR} ${prog}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(lib_${prog} PROPERTIES LABELS lib)
    endif()

    # The disk-image half: copy the image just linked into ${B6_ROOTFS}/usr/test/.
    #
    # A POST_BUILD STEP ON lib_${prog}_img RATHER THAN A TARGET OF ITS OWN, and the reason is
    # the one b6_obj()'s comment gives about shared objects.  A second custom target consuming
    # ${CMAKE_CURRENT_BINARY_DIR}/${prog} would make the Makefiles generator emit the b6ld rule
    # into BOTH targets' build.make, so `make -j' could run two links onto the same output at
    # once -- and it would need its own add_dependencies(... libc) besides, since a file-level
    # DEPENDS does not order two directory scopes.  Hanging the copy off the target that
    # already builds the file has neither problem.
    #
    # copy_if_different, so an unchanged program does not re-stamp the file and re-trigger the
    # whole disk image; `rootfs' is the aggregate every stager hangs on, and the top-level
    # CMakeLists.txt creates it BEFORE add_subdirectory(lib) so that this guard is true here.
    if(NOT "SIMONLY" IN_LIST _argn)
        add_custom_command(TARGET lib_${prog}_img POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/test
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_BINARY_DIR}/${prog} ${B6_ROOTFS}/usr/test/${prog}
            COMMENT "stage usr/test/${prog}" VERBATIM)
        if(TARGET rootfs)
            add_dependencies(rootfs lib_${prog}_img)
        endif()

        # The two USER address-space ceilings, as b6_prog() checks them: 28 pages of image
        # (32 less the four the stack takes at 070000) and the 15-bit reach of a pointer.
        # Both failures are otherwise silent -- the link succeeds and only the running
        # program misbehaves.  pwent is the largest of these at 7,560 words.
        add_test(NAME rootfs_${prog}_size
            COMMAND sh ${B6_SCRIPTS_DIR}/check-size.sh
                    ${B6SIZE} ${B6NM} ${B6_ROOTFS}/usr/test/${prog} 28672 32767)
        set_tests_properties(rootfs_${prog}_size PROPERTIES LABELS rootfs)
    endif()
endmacro()

b6_libtest(hello)
b6_libtest(vararg)
b6_libtest(errno)
b6_libtest(procs)
b6_libtest(sbrkt)
b6_libtest(malloct)
b6_libtest(strings)
b6_libtest(gen)
b6_libtest(strtolt)
b6_libtest(environ)
b6_libtest(jmp)
b6_libtest(headers)
b6_libtest(stdiot)
b6_libtest(printft)
b6_libtest(scanft)
b6_libtest(execs)
b6_libtest(spawn SIMONLY)
b6_libtest(shellt IMAGEONLY)
b6_libtest(memt IMAGEONLY)
b6_libtest(kctlt)
b6_libtest(suidt IMAGEONLY)
b6_libtest(unprivt IMAGEONLY)
b6_libtest(timet)
b6_libtest(pwent)
b6_libtest(ttyt IMAGEONLY)
b6_libtest(dirt IMAGEONLY)
b6_libtest(signals)
b6_libtest(matht libm)
b6_libtest(termcapt libtermcap)
b6_libtest(cursest libcurses libtermcap)
b6_libtest(curstty libcurses libtermcap IMAGEONLY)
b6_libtest(puret PURE)

# Aggregate target the top-level `make test' builds before ctest runs (ctest never builds).
add_custom_target(lib_tests DEPENDS ${TEST_TARGETS})

# Hook the images into the top-level build_tests aggregate, so `make test' (which builds
# build_tests) and `make run' (ctest) cover the library tests too.
if(TARGET build_tests)
    add_dependencies(build_tests ${TEST_TARGETS})
endif()
