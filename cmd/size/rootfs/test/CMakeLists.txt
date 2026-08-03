# Tests for /usr/bin/size, the native build of cmd/size (task C9c).  Two kinds,
# and they assert different things -- run-size-test.sh's header has the argument:
#
#   rootfs_size_<case>  the AGREEMENT tests.  Host b6size and native size over one
#                       object, the two listings diffed live.  Says the port is
#                       faithful; says nothing about whether the numbers are right,
#                       since two sizes wrong the same way agree.
#   cmd_size_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                       output, the diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_size_size, which is the other assertion about this program AS BUILT
# FOR THE TARGET.  (That name is b6_prog()'s rootfs_<name>_size over a program
# called size; it is not a typo.)

# The host tools as generator expressions: build/cmd/size/b6size, not an installed
# b6size.  The targets resolve because cmd/size is added at the top of the
# top-level CMakeLists.txt, long before this directory is reached from inside the
# libruntime guard.  An INSTALLED b6size would be a different build of the same
# source and the comparison would say nothing.
# name is the ctest case, fixture the NAME.s it assembles; the rest are size flags.
# They are separate arguments because the same fixture is measured several ways.
function(size_agree name fixture)
    add_test(NAME rootfs_size_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-size-test.sh
                $<TARGET_FILE:b6size> ${B6SIM} ${B6_ROOTFS}/usr/bin/size
                $<TARGET_FILE:b6as>
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${fixture} ${ARGN})
    set_tests_properties(rootfs_size_${name} PROPERTIES LABELS rootfs)
endfunction()

size_agree(bytes agree)      # the default: four segments and their sum, in bytes
size_agree(words agree -w)   # ... and in words, which is what every size check reads

# ctest does not build.  The staged program comes along through b6_progtest below,
# but the host tools and this directory's fixture are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6size b6as b6prog_size)
endif()

# The behavioural cases.  size names its input files on the command line, so a
# diagnostic can carry a path -- `notfound' names one relative to the working
# directory, and the other two pass no file at all.  argv[0] is the staged
# absolute path, but every message prints only its basename, so the "size:"
# prefix is the same in both worlds.
b6_progtest(size badflag  DEST usr/bin/size) # an unknown flag: the message and status 1
b6_progtest(size noargs   DEST usr/bin/size) # no arguments: the usage summary, and status 1
b6_progtest(size notfound DEST usr/bin/size) # an input that is not there -- and status 0
