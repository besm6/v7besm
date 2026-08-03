# Tests for /usr/bin/disasm, the native build of cmd/disasm (task C9c).  Two kinds,
# and they assert different things -- run-disasm-test.sh's header has the argument:
#
#   rootfs_disasm_<case>  the AGREEMENT tests.  Host b6disasm and native disasm over
#                         one object, the two listings diffed live.  Says the port is
#                         faithful; says nothing about whether the decoding is right,
#                         since two disassemblers wrong the same way agree.
#   cmd_disasm_<case>     ordinary b6sim cases with a checked-in .expected.  Say the
#                         diagnostics and the exit status are right.
#
# The agreement cases carry label `rootfs' rather than `cmd': they are of a kind
# with rootfs_disasm_size, which is the other assertion about this program AS BUILT
# FOR THE TARGET.

# The host tools as generator expressions: build/cmd/disasm/b6disasm, not an
# installed b6disasm.  The targets resolve because cmd/disasm is added at the top of
# the top-level CMakeLists.txt, long before this directory is reached from inside
# the libruntime guard.  An INSTALLED b6disasm would be a different build of the
# same source and the comparison would say nothing.
# name is the ctest case, fixture the NAME.s it assembles, kind `obj' or `image';
# the rest are disasm flags.  They are separate arguments because one fixture is
# decoded several ways.
function(disasm_agree name fixture kind)
    add_test(NAME rootfs_disasm_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-disasm-test.sh
                $<TARGET_FILE:b6disasm> ${B6SIM} ${B6_ROOTFS}/usr/bin/disasm
                $<TARGET_FILE:b6as> $<TARGET_FILE:b6ld>
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${fixture} ${kind} ${ARGN})
    set_tests_properties(rootfs_disasm_${name} PROPERTIES LABELS rootfs)
endfunction()

disasm_agree(obj    agree obj)          # both instruction formats, both operand printings
disasm_agree(reloc  agree obj -r)       # ... with the relocation records read symbolically
disasm_agree(relnum agree obj -R)       # ... and as numbers
disasm_agree(image  agree image -c)     # a LINKED image, and the raw octal beside each word
disasm_agree(octal  agree image -C)     # ... octal only, the decoder skipped entirely
disasm_agree(bemsh  agree image -b)     # the Cyrillic dialect: UTF-8 literals through b6cc

# ctest does not build.  The staged program comes along through b6_progtest below,
# but the host tools and this directory's fixture are this file's business.
if(TARGET build_tests)
    add_dependencies(build_tests b6disasm b6as b6ld b6prog_disasm)
endif()

# The behavioural cases.  disasm names its input files on the command line, so a
# diagnostic can carry a path -- `notfound' names one relative to the working
# directory, and `noargs' passes nothing at all.  argv[0] is the staged absolute
# path, but every message prints only its basename, so the "disasm:" prefix is the
# same in both worlds.
b6_progtest(disasm badflag  DEST usr/bin/disasm) # an unknown flag: usage(), and status 1
b6_progtest(disasm noargs   DEST usr/bin/disasm) # no arguments: the same summary
b6_progtest(disasm notfound DEST usr/bin/disasm) # an input that is not there
