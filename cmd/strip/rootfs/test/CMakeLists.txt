# Tests for /usr/bin/strip, the native build of cmd/strip (task C9c).  Two kinds,
# and they assert different things -- run-strip-test.sh's header has the argument:
#
#   rootfs_strip_<case>  the AGREEMENT tests.  Host b6strip and native strip over
#                        one object apiece, the two results compared live.  Says
#                        the port is faithful; says nothing about whether the
#                        stripping is right, since two strips wrong the same way
#                        agree.
#   cmd_strip_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                        diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_strip_size, which is the other assertion about this program AS BUILT
# FOR THE TARGET.

# The host tools as generator expressions: build/cmd/strip/b6strip, not an
# installed b6strip.  The targets resolve because cmd/strip is added at the top of
# the top-level CMakeLists.txt, long before this directory is reached from inside
# the libruntime guard.  An INSTALLED b6strip would be a different build of the
# same source and the comparison would say nothing.
# name is the ctest case and fixture the NAME.s it assembles; they are separate
# because one fixture is stripped more than one way.
function(strip_agree name fixture)
    add_test(NAME rootfs_strip_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-strip-test.sh
                $<TARGET_FILE:b6strip> ${B6SIM} ${B6_ROOTFS}/usr/bin/strip
                $<TARGET_FILE:b6as> $<TARGET_FILE:b6nm>
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${fixture} ${ARGN})
    set_tests_properties(rootfs_strip_${name} PROPERTIES LABELS rootfs)
endfunction()

strip_agree(obj agree)  # a 600-word const block: more than one target-side chunk

# ctest does not build.  The staged program comes along through b6_progtest below,
# but the host tools and this directory's fixture are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6strip b6as b6nm b6prog_strip)
endif()

# The behavioural cases.  strip names its input files on the command line, so a
# diagnostic can carry a path -- `notfound' names one relative to the working
# directory, and `noargs' passes nothing at all.  argv[0] is the staged absolute
# path, but every message prints only its basename, so the "strip:" prefix is the
# same in both worlds.  There is no case for the successful rewrite here: that one
# is the agreement test above, an .expected having nothing to say about a program
# whose whole output is a changed file.  Nor is there one for a file that exists but
# is not an a.out: every way of naming such a file puts a path into the diagnostic,
# and run-prog-test.sh's @srcdir@ is an absolute build-machine path no .expected can
# hold.  ../test/'s Strip.NotAnObject covers that arm on the host.
b6_progtest(strip noargs   DEST usr/bin/strip) # no arguments: the usage summary, status 1
b6_progtest(strip notfound DEST usr/bin/strip) # an input that is not there: status 1
