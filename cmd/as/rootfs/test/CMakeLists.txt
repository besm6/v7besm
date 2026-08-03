# Tests for /usr/bin/as, the native build of cmd/as (task C9b).  Two kinds, and
# they assert different things -- run-as-test.sh's header has the argument:
#
#   rootfs_as_<case>  the AGREEMENT tests.  Host b6as and native as over one
#                     fixture, the two objects compared live.  Says the port is
#                     faithful; says nothing about whether the object is right,
#                     since two assemblers wrong the same way agree.
#   cmd_as_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                     diagnostics and the exit status are right -- including the
#                     one limit the BESM-6 build has and the host does not.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_as_size, which is the other assertion about this program AS BUILT
# FOR THE TARGET.

# The host tool as a generator expression: build/cmd/as/b6as, not an installed
# b6as.  The target resolves because cmd/as is added at the top of the top-level
# CMakeLists.txt, long before this directory is reached from inside the
# libruntime guard.  An INSTALLED b6as would be a different build of the same
# source and the comparison would say nothing.
function(as_agree case)
    add_test(NAME rootfs_as_${case}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-as-test.sh
                $<TARGET_FILE:b6as> ${B6SIM} ${B6_ROOTFS}/usr/bin/as $<TARGET_FILE:b6nm>
                ${CMAKE_CURRENT_SOURCE_DIR} ${case} ${ARGN})
    set_tests_properties(rootfs_as_${case} PROPERTIES LABELS rootfs)
endfunction()

as_agree(agree)     # the assembler's own surface: every base, operator and operand form
as_agree(compiled)  # ... and real b6cc output, which is what it will actually be fed

# ctest does not build.  The staged program comes along through b6_progtest
# below, but the host tools and this directory's fixtures are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6as b6nm b6prog_as)
endif()

# The behavioural cases.  Each feeds its source on STANDARD INPUT and names no
# input file, so a diagnostic carries no path: run-prog-test.sh would otherwise
# pass the fixture's absolute build-machine path as an argument, and no
# .expected could hold that.  argv[0] is the staged path, but fatal() prints
# only its basename, so the "as:" prefix is the same in both worlds.
b6_progtest(as badflag DEST usr/bin/as) # an unknown option: usage()'s exit status
b6_progtest(as undef   DEST usr/bin/as) # -u, and the name in the message
b6_progtest(as deep    DEST usr/bin/as) # past MAXEXPRDEPTH, which only the target has
