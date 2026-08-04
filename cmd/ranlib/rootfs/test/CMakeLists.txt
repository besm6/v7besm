# Tests for /usr/bin/ranlib, the native build of cmd/ranlib (task C9d).  Two kinds,
# and they assert different things -- run-ranlib-test.sh's header has the argument:
#
#   rootfs_ranlib_<case>  the AGREEMENT tests.  Host b6ranlib and native ranlib over
#                         one archive, the results compared byte for byte except the
#                         one word fixdate() stamps with the time of day.
#   cmd_ranlib_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                         diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs', of a kind with rootfs_ranlib_size.
#
# The fixtures are cmd/ar/rootfs/test's, on purpose: this program CONTAINS all of
# cmd/ar, so the two suites indexing different members would be two answers to one
# question.  The native ar is passed in as well, for the `chain' case.
function(ranlib_agree name kind)
    add_test(NAME rootfs_ranlib_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-ranlib-test.sh
                $<TARGET_FILE:b6ranlib> $<TARGET_FILE:b6ar> ${B6SIM}
                ${B6_ROOTFS}/usr/bin/ranlib ${B6_ROOTFS}/usr/bin/ar
                $<TARGET_FILE:b6as> ${B6_LIBC_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${kind})
    set_tests_properties(rootfs_ranlib_${name} PROPERTIES LABELS rootfs)
endfunction()

ranlib_agree(index  masked) # no __.SYMDEF yet: fixsize()'s `new' branch, ar rlb
ranlib_agree(rerun  masked) # ...and then the replace-in-place one, ar rl
ranlib_agree(touch  masked) # -t: fixdate() alone must move that one word and no other
ranlib_agree(symdef symdef) # the index MEMBER compared exactly, nothing masked
ranlib_agree(multi  masked) # two archives in one run: ar_run() twice, and the
                            # descriptors cmd/ar's finish() now closes
ranlib_agree(libc   masked) # the real corpus: 222 symbols over 154 members
ranlib_agree(chain  masked) # ar AND ranlib native, over the C library's own objects --
                            # the machine builds its own libc.a, archive and index both

# ctest does not build.  The staged programs come along through b6_progtest below,
# but the host tools, the libc objects the `chain' case archives, and cmd/ar's
# fixtures are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6ranlib b6ar b6as b6prog_ranlib b6prog_ar libc)
endif()

# The behavioural cases.  Every argument is a relative name and every message prints
# only argv[0]'s basename, so the "ranlib:" prefix is the same in both worlds.
b6_progtest(ranlib noargs   DEST usr/bin/ranlib) # no arguments: usage, status 1
b6_progtest(ranlib badflag  DEST usr/bin/ranlib) # an unknown flag -- which USED to call
                                                 # exit(1) from inside the engine
b6_progtest(ranlib notfound DEST usr/bin/ranlib) # an archive that is not there, and
                                                 # status 0: the loop just continues
