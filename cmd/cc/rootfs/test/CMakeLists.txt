# Tests for /usr/bin/cc, the native build of cmd/cc (task C9e).  Two kinds, and
# they assert different things -- run-cc-test.sh's header has the argument:
#
#   rootfs_cc_<case>  the AGREEMENT tests.  Host b6cc and native cc over one set of
#                     inputs, the files the two runs produced compared byte for
#                     byte.  Says the port is faithful -- that the driver named the
#                     same input, output and flags at every stage -- and says
#                     nothing about whether those flags are right, since two ccs
#                     wrong the same way agree.
#   cmd_cc_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                     diagnostics and the exit status are right, which is the half
#                     the agreement cases cannot reach.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_cc_size, which is the other assertion about this program AS BUILT FOR
# THE TARGET.
#
# The host tools as generator expressions: build/cmd/*/b6*, not installed ones.  The
# targets resolve because cmd/cc, cmd/cpp, cmd/as and cmd/ld are all added at the top
# of the top-level CMakeLists.txt, long before this directory is reached from inside
# the libruntime guard.  An INSTALLED b6cc would be a different build of the same
# source and the comparison would say nothing.
#
# name is the ctest case AND the scenario run-cc-test.sh selects; kind is the
# space-separated list of files compared on top of standard output.
function(cc_agree name kind)
    add_test(NAME rootfs_cc_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-cc-test.sh
                $<TARGET_FILE:b6cc> ${B6SIM} ${B6_ROOTFS}/usr/bin/cc
                $<TARGET_FILE:b6cpp> $<TARGET_FILE:b6as> $<TARGET_FILE:b6ld>
                ${B6_ROOTFS} ${B6_LIBC_DIR} ${B6LIBDIR}
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${kind})
    set_tests_properties(rootfs_cc_${name} PROPERTIES LABELS rootfs)
endfunction()

cc_agree(assemble "X.o")            # -c on a .s: one stage, and the -o name honoured
cc_agree(preasm   "X.o")            # -c on a .S: cpp into as, through a temp file
cc_agree(cpp      "X.i")            # -E: the stage that stops first
cc_agree(defines  "X.i")            # -D and -I in their SEPARATED form, which cc folds
cc_agree(link     "X")              # the link: crt0, the objects, the flag order
cc_agree(multi    "agree.o preasm.o X") # two compiles with no -o, then a link of one

# ctest does not build.  The staged program comes along through b6_progtest below,
# but the host tools and the crt0.o the link cases name are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6cc b6cpp b6as b6ld b6prog_cc libc)
endif()

# The behavioural cases.  Every argument is a RELATIVE name and every message prints
# only argv[0]'s basename, so the "cc:" prefix and the whole line are the same in
# both worlds.
b6_progtest(cc noargs    DEST usr/bin/cc) # no arguments: the usage summary, status 1
b6_progtest(cc badflag   DEST usr/bin/cc) # an unknown option, and the usage after it
b6_progtest(cc badsuffix DEST usr/bin/cc) # an input the suffix dispatch cannot place
b6_progtest(cc cfile     DEST usr/bin/cc) # THE LIMITATION, pinned: a .c file on this
                                          # machine gets an honest sentence rather
                                          # than a missing-file error.  ../../README.md,
                                          # "It cannot compile C"
b6_progtest(cc multiout  DEST usr/bin/cc) # -o with -c and two inputs: refused
