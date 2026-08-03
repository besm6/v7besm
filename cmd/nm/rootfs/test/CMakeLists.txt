# Tests for /usr/bin/nm, the native build of cmd/nm (task C9c).  Two kinds, and
# they assert different things -- run-nm-test.sh's header has the argument:
#
#   rootfs_nm_<case>  the AGREEMENT tests.  Host b6nm and native nm over one object
#                     or archive, the two listings diffed live.  Says the port is
#                     faithful; says nothing about whether the listing is right,
#                     since two nms wrong the same way agree.
#   cmd_nm_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                     diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_nm_size, which is the other assertion about this program AS BUILT FOR
# THE TARGET -- and the one that matters most here, QUANT being a heap number the
# size check cannot see.

# The host tools as generator expressions: build/cmd/nm/b6nm, not an installed
# b6nm.  The targets resolve because cmd/nm is added at the top of the top-level
# CMakeLists.txt, long before this directory is reached from inside the libruntime
# guard.  An INSTALLED b6nm would be a different build of the same source and the
# comparison would say nothing.
# name is the ctest case, fixture the NAME*.s set it assembles, kind `obj', `objs'
# or `archive'; the rest are nm flags.  They are separate arguments because one
# fixture is listed several ways.
function(nm_agree name fixture kind)
    add_test(NAME rootfs_nm_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-nm-test.sh
                $<TARGET_FILE:b6nm> ${B6SIM} ${B6_ROOTFS}/usr/bin/nm
                $<TARGET_FILE:b6as> $<TARGET_FILE:b6ar>
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${fixture} ${kind} ${ARGN})
    set_tests_properties(rootfs_nm_${name} PROPERTIES LABELS rootfs)
endfunction()

nm_agree(obj     agree obj)          # every symbol class, sorted by name
nm_agree(num     agree obj -n)       # ... by value instead, which is a different order
nm_agree(rev     agree obj -n -r)    # ... and backwards
nm_agree(nosort  agree obj -p)       # ... and not at all: symbol-table order
nm_agree(globl   agree obj -g)       # the external filter
nm_agree(undef   agree obj -u)       # the undefined filter, which drops the value column
nm_agree(prepend agree objs -o)      # two files: the name on every line, and no banner
nm_agree(files   agree objs)         # ... and with the banner, which is the other branch
nm_agree(archive agree archive)      # a LIBRARY: fgetarhdr(), and the per-member loop
nm_agree(archnum agree archive -n)   # ... sorted per member, not once for the archive

# ctest does not build.  The staged program comes along through b6_progtest below,
# but the host tools and this directory's fixtures are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6nm b6as b6ar b6prog_nm)
endif()

# The behavioural cases.  nm names its input files on the command line, so a
# diagnostic can carry a path -- `notfound' names one relative to the working
# directory, and `noargs' passes nothing at all.  argv[0] is the staged absolute
# path, but every message prints only its basename, so the "nm:" prefix is the same
# in both worlds.
b6_progtest(nm badflag  DEST usr/bin/nm) # an unknown flag: the message and status 1
b6_progtest(nm noargs   DEST usr/bin/nm) # no arguments: the usage summary, and status 1
b6_progtest(nm notfound DEST usr/bin/nm) # an input that is not there -- and status 0
