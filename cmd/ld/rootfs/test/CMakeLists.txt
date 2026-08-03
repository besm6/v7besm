# Tests for /usr/bin/ld, the native build of cmd/ld (task C9b).  Two kinds, and
# they assert different things -- run-ld-test.sh's header has the argument:
#
#   rootfs_ld_<case>  the AGREEMENT tests.  Host b6ld and native ld over one set
#                     of objects, the two images compared live.  Says the port is
#                     faithful; says nothing about whether the image is right,
#                     since two linkers wrong the same way agree.
#   cmd_ld_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                     diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_ld_size, which is the other assertion about this program AS BUILT
# FOR THE TARGET.

# The host tools as generator expressions: build/cmd/ld/b6ld, not an installed
# b6ld.  The targets resolve because cmd/ld is added at the top of the top-level
# CMakeLists.txt, long before this directory is reached from inside the
# libruntime guard.  An INSTALLED b6ld would be a different build of the same
# source and the comparison would say nothing.
# name is the ctest case, fixture the NAME*.s set it links; the rest are ld flags.
# They are separate arguments because the same fixture is linked several ways.
function(ld_agree name fixture)
    add_test(NAME rootfs_ld_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-ld-test.sh
                $<TARGET_FILE:b6ld> ${B6SIM} ${B6_ROOTFS}/usr/bin/ld
                $<TARGET_FILE:b6nm> $<TARGET_FILE:b6as>
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${fixture} ${ARGN})
    set_tests_properties(rootfs_ld_${name} PROPERTIES LABELS rootfs)
endfunction()

ld_agree(link    link)      # two objects: const-pool merging, externals both ways, commons
ld_agree(link_r  link -r)   # ... relinkable output, which opens all ten scratch streams
ld_agree(link_x  link -x)   # ... and with the local symbols discarded
ld_agree(link_n  link -n)   # ... NMAGIC, whose page padding finish_output() writes

# ctest does not build.  The staged program comes along through b6_progtest
# below, but the host tools and this directory's fixtures are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6ld b6nm b6as b6prog_ld)
endif()

# The behavioural cases.  ld names its input files on the command line, so a
# diagnostic can carry a path -- these two are chosen so that none does: `noargs'
# passes nothing at all, and `badfile' names a file relative to the working
# directory.  argv[0] is the staged absolute path, but error() prints only its
# basename, so the "ld:" prefix is the same in both worlds.
b6_progtest(ld noargs  DEST usr/bin/ld) # no arguments: the usage summary, and status 4
b6_progtest(ld badfile DEST usr/bin/ld) # an input that is not there
