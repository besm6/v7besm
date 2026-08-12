# Tests for /usr/bin/cpp, the native build of cmd/cpp (task C9a).  Two kinds, and they
# assert different things -- ../README.md SS5 and run-cpp-test.sh's header have the argument:
#
#   rootfs_cpp_<case>  the AGREEMENT tests.  Host b6cpp and native cpp over one fixture,
#                      diffed live.  Says the port is faithful; says nothing about whether
#                      the output is right, since two cpps wrong the same way agree.
#   cmd_cpp_<case>     ordinary b6sim cases with a checked-in .expected.  Say the output
#                      and the exit status are right, and pin the diagnostics -- including
#                      the one warning the BESM-6 build issues and the host does not.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind with
# rootfs_cpp_size, which is the other assertion about this program AS BUILT FOR THE TARGET.

# The host tool as a generator expression: build/cmd/cpp/b6cpp, not an installed b6cpp.
# The target resolves because cmd/cpp is added at the top of the top-level CMakeLists.txt,
# long before this directory is reached from inside the libruntime guard.  An INSTALLED
# b6cpp would be a different build of the same source and the diff would say nothing.
function(cpp_agree case)
    add_test(NAME rootfs_cpp_${case}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-cpp-test.sh
                $<TARGET_FILE:b6cpp> ${B6SIM} ${B6_ROOTFS}/usr/bin/cpp
                ${CMAKE_CURRENT_SOURCE_DIR} ${case} ${ARGN}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rootfs_cpp_${case} PROPERTIES LABELS rootfs)
endfunction()

cpp_agree(agree)  # the ordinary preprocessor surface, inside every BESM-6 limit
cpp_agree(deep)   # ... and past the argument-nesting bound, which must still agree

# ctest does not build.  The staged program comes along through b6_progtest below, but the
# host tool and this directory's own fixtures are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6cpp b6prog_cpp)
endif()

# The behavioural cases.  Each feeds its source on STANDARD INPUT and names no input file,
# so a diagnostic carries no path: run-prog-test.sh passes the fixture's absolute build-
# machine path as an argument otherwise, and no .expected could hold that.
b6_progtest(cpp nesting DEST usr/bin/cpp) # the MAXARGDEPTH warning, and status 0 with it
b6_progtest(cpp predef  DEST usr/bin/cpp) # `besm6' is defined here and is #undef-able
b6_progtest(cpp dopt    DEST usr/bin/cpp) # -D and -U on the command line
b6_progtest(cpp noinc   DEST usr/bin/cpp) # a header that is not there: the message and the status
b6_progtest(cpp badflag DEST usr/bin/cpp) # an unknown option: usage()'s exit status, 8
