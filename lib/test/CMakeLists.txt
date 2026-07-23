# Test programs for the user-level libraries, run under b6sim.
#
# b6sim is the harness until a root filesystem exists: it runs one user a.out and services the
# v7 syscalls on the host, far faster than booting SIMH.  Each program is linked against the
# real crt0.o and libc.a, run, and its output diffed against the checked-in .expected file (see
# run-test.sh); a program that needs arguments lists them in a matching .args file.

# The sibling libraries' build outputs -- crt0.o and libc.a next door in ../libc, libm.a in
# ../libm.  Normalized so the `..' does not reach the b6ld command line.
get_filename_component(LIBC_DIR ${CMAKE_CURRENT_BINARY_DIR}/../libc ABSOLUTE)
get_filename_component(LIBM_DIR ${CMAKE_CURRENT_BINARY_DIR}/../libm ABSOLUTE)
set(CRT0   ${LIBC_DIR}/crt0.o)
set(LIBC_A ${LIBC_DIR}/libc.a)
set(LIBM_A ${LIBM_DIR}/libm.a)

set(TEST_TARGETS "")

# Link one test program: crt0.o first, its own object, then the libraries.  Link order is the
# contract -- [-lm] then -lc then -lruntime, b6ld scanning each archive once (libm calls into
# libc, libc into libruntime, never back).  Pass `libm' in ARGN for the one program that needs
# it.  add_dependencies pins the sibling libraries ahead of the link across directory scopes.
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
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${prog}
        COMMAND ${B6LD} ${CRT0} ${_o} ${_mlib} -o ${prog} -L${LIBC_DIR} -L${B6LIBDIR} -lc -lruntime
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
    if(B6SIM)
        add_test(NAME lib_${prog}
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-test.sh ${B6SIM} ${CMAKE_CURRENT_SOURCE_DIR} ${prog}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(lib_${prog} PROPERTIES LABELS lib)
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
b6_libtest(environ)
b6_libtest(jmp)
b6_libtest(headers)
b6_libtest(stdiot)
b6_libtest(printft)
b6_libtest(scanft)
b6_libtest(execs)
b6_libtest(spawn)
b6_libtest(timet)
b6_libtest(pwent)
b6_libtest(signals)
b6_libtest(matht libm)

# Aggregate target the top-level `make test' builds before ctest runs (ctest never builds).
add_custom_target(lib_tests DEPENDS ${TEST_TARGETS})

# Hook the images into the top-level build_tests aggregate, so `make test' (which builds
# build_tests) and `make run' (ctest) cover the library tests too.
if(TARGET build_tests)
    add_dependencies(build_tests ${TEST_TARGETS})
endif()
