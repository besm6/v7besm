# Tests for /usr/bin/ar, the native build of cmd/ar (task C9d).  Two kinds, and
# they assert different things -- run-ar-test.sh's header has the argument:
#
#   rootfs_ar_<case>  the AGREEMENT tests.  Host b6ar and native ar over one set of
#                     inputs, the two archives compared byte for byte.  Says the
#                     port is faithful; says nothing about whether the archive is
#                     right, since two ars wrong the same way agree.
#   cmd_ar_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                     diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_ar_size, which is the other assertion about this program AS BUILT FOR
# THE TARGET.
#
# The host tools as generator expressions: build/cmd/ar/b6ar, not an installed one.
# The targets resolve because cmd/ar is added at the top of the top-level
# CMakeLists.txt, long before this directory is reached from inside the libruntime
# guard.  An INSTALLED b6ar would be a different build of the same source and the
# comparison would say nothing.
#
# name is the ctest case AND the scenario run-ar-test.sh selects; kind is what gets
# compared on top of standard output -- `archive', `stdout' or `extract'.
function(ar_agree name kind)
    add_test(NAME rootfs_ar_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-ar-test.sh
                $<TARGET_FILE:b6ar> ${B6SIM} ${B6_ROOTFS}/usr/bin/ar
                $<TARGET_FILE:b6as> ${B6_LIBC_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${kind})
    set_tests_properties(rootfs_ar_${name} PROPERTIES LABELS rootfs)
endfunction()

ar_agree(create  archive) # cr: the first mkstemp, putint's ARMAG, putarhdr, commit
ar_agree(replace archive) # r over an existing member: next_member, the IODD+SKIP skip
ar_agree(update  archive) # ru with a file no newer than the member: the -u branch
ar_agree(delete  archive) # d: copy everything through but the named member
ar_agree(before  archive) # rb: the SECOND mkstemp, and the two-way join in commit
ar_agree(move    archive) # ma: the THIRD mkstemp, and the three-way one
ar_agree(quick   archive) # q: the only command that writes the archive in place
ar_agree(local   archive) # -l: temporaries in the cwd rather than in /tmp
ar_agree(toc     stdout)  # t: the plain listing
ar_agree(tocv    stdout)  # tv: the ONLY case that reaches list.c -- and ctime, in UTC
ar_agree(print   stdout)  # p: member bytes straight through fd 1
ar_agree(extract extract) # x: creat() per member, and the count_pending early exit
ar_agree(libc    archive) # the machine archives the C library it was linked against

# ctest does not build.  The staged program comes along through b6_progtest below,
# but the host tools, the libc objects the `libc' case archives, and this
# directory's fixtures are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6ar b6as b6prog_ar libc)
endif()

# The behavioural cases.  Every argument is a RELATIVE name and every message
# prints only argv[0]'s basename, so the "ar:" prefix and the whole line are the
# same in both worlds.
b6_progtest(ar noargs    DEST usr/bin/ar) # no arguments: the usage summary, status 1
b6_progtest(ar badflag   DEST usr/bin/ar) # an unknown flag letter
b6_progtest(ar nocommand DEST usr/bin/ar) # option letters but no command letter
b6_progtest(ar twocmd    DEST usr/bin/ar) # two command letters at once
b6_progtest(ar notfound  DEST usr/bin/ar) # an archive that is not there -- and status 1,
                                          # where cmd_nm_notfound is status 0
b6_progtest(ar qab       DEST usr/bin/ar) # q and the positioning options together
