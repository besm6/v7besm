# b6sim cases for /bin/tar -- task C7.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.expected, an optional <case>.status and an optional <case>.in, and
# run-prog-test.sh diffs stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): tar prints the name it was given in every diagnostic, so a case that named a file
# through an absolute build path could not have a checked-in .expected.
#
# ref.tar IS A CHECKED-IN BINARY FIXTURE, and it is the point of this directory.  It was made
# once by the HOST's tar with `--format=v7 --uid 0 --gid 0' over three files stamped
# 1979-01-02 and chmod'd 644, 600 and 755 -- so every field of every header holds a value that
# nothing about this machine or this build can move.  `cmd_tar_listv' diffs the guest's
# `tar tv' of it against a checked-in listing, which is what pins the mode field to offset
# 100, the size to 124, the mtime to 136 and the linkflag to 156.  Had a struct member been
# word-aligned here -- as ../../README.md and ../../mount/README.md both claimed until this
# task measured it -- every one of those columns would move and this case would say so.
#
# THE DATE COLUMN IS SAFE IN A CHECKED-IN FILE, which is not obvious and is worth saying:
# lib/libc/gen/ctime.c takes the zone from ftime(2), and under b6sim that answers zone 0 and
# DST 0, so localtime() and gmtime() agree and the listing does not depend on the developer's
# TZ.  Under the booted kernel it would be the machine's own, which is one more reason the
# tree walk is asserted in kernel/test/tar and not here.
#
# bad.tar IS ref.tar WITH ONE BYTE OF THE FIRST NAME CHANGED, so the stored checksum no longer
# matches what checksum() computes.  ../../README.md's C4d rule -- a test that detects damage
# must assert that there was damage to find -- is met from both ends: cmd_tar_badsum requires
# the diagnostic AND status 2, and cmd_tar_files reads the undamaged copy and gets three names.
#
# NONE OF THE FIXTURE NAMES ENDS IN `.out': run-prog-test.sh captures stdout as `<case>.out'
# in the working directory and would truncate a fixture of that name before the program opened
# it (§9's sixth limit, which cost cmd/file/test half an hour).
#
# WHAT IS NOT HERE, and ../README.md says it at length: the tree walk.  b6sim refuses to read a
# directory descriptor -- ls(1)'s, du(1)'s and find(1)'s limit -- so `tar c' of a DIRECTORY
# cannot run under this harness at all, and neither can checkdir()'s exec of /bin/mkdir, the
# owner and mode a `tar x' restores, or anything on /dev/rmd?.  All of that is kernel/test/tar,
# on volume 3099.

set(TAR_FIXTURES ref.tar bad.tar alpha beta cyr)
set(_copied)
foreach(fx ${TAR_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${fx} ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fx}
        COMMENT "cmd/tar/test: ${fx}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${fx})
endforeach()
add_custom_target(tar_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests tar_fixtures)
endif()

b6_progtest(tar list)     # the names in a host-made v7 archive
b6_progtest(tar listv)    # ... and every other field of every header: the layout assertion
b6_progtest(tar stdin)    # `tf -', the form the manual page's pipeline idiom is built on
b6_progtest(tar badsum)   # a header whose stored checksum does not match: refused, status 2
b6_progtest(tar nokey)    # an unknown key letter
b6_progtest(tar nofile)   # no `f': mandatory here, where v7 fell back on /dev/mt1
b6_progtest(tar noarg)    # `f' with nothing after it
b6_progtest(tar bigb)     # a blocking factor above NBLOCK
b6_progtest(tar nosuch)   # a named file that is not there -- diagnosed, and v7's status kept
b6_progtest(tar blockedr) # `r' onto a blocked archive, which cannot be positioned

# The FILE oracle: what tar writes cannot be judged from stdout.  run-tar-test.sh's header is
# the account of the six things it asserts, of which the blocking factor is the one this port
# exists for.
if(B6SIM)
    add_test(NAME cmd_tar_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-tar-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/tar ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_tar_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_tar)
        add_dependencies(build_tests b6prog_tar)
    endif()
endif()
