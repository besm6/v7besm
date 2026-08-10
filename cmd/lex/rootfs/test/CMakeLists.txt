# Tests for /usr/bin/lex, the native build of cmd/lex (task C10d).  Two kinds:
#
#   rootfs_lex_<case>   AGREEMENT.  Host b6lex and native lex over one scanner,
#                       diffed live.  Says the port is faithful, not that the
#                       scanner is right -- two lexes wrong the same way agree.
#   cmd_lex_<case>      b6sim cases with a checked-in .expected: the diagnostics.
#
# The agreement cases carry label `rootfs', being of a kind with rootfs_lex_size.
# b6lex as a generator expression, never an installed one, which would be a
# different build of the same source and would say nothing.
function(lex_agree name scanner)
    add_test(NAME rootfs_lex_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-lex-test.sh
                $<TARGET_FILE:b6lex> ${B6SIM} ${B6_ROOTFS}/usr/bin/lex
                ${CMAKE_CURRENT_SOURCE_DIR}/../../ncform
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${scanner} ${ARGN})
    set_tests_properties(rootfs_lex_${name} PROPERTIES LABELS rootfs)
endfunction()

# BOTH THE .l FILES THIS TREE HAS, and no fixture written for the suite: a
# scanner written here could be sized to fit the profile it is meant to test.
# awk.lx.l is what the besm6 arm was measured against.  The shapes neither
# reaches -- REJECT, yyless(), a char-packed state -- stay in ../../test/, which
# compiles a generated scanner and runs it.
lex_agree(awk   ${CMAKE_SOURCE_DIR}/cmd/awk/awk.lx.l)   # C17
lex_agree(scant ${CMAKE_CURRENT_SOURCE_DIR}/../scant.l)

# ctest does not build; the staged program arrives through b6_progtest below.
if(TARGET build_tests)
    add_dependencies(build_tests b6lex b6prog_lex)
endif()

# run-prog-test.sh runs `env -i', so B6LEXFORM does not reach the guest and
# /usr/lib/lex/ncform is not there.  noskel is that case and asserts the path
# find_form()'s besm6 arm names; the other two fail before the skeleton is wanted.
# Every case reads its scanner from STDIN, an argument being an absolute path
# that error() would print.
b6_progtest(lex noskel   DEST usr/bin/lex) # a whole scanner, and no skeleton to append
b6_progtest(lex notfound DEST usr/bin/lex) # an input file that is not there
b6_progtest(lex badflag  DEST usr/bin/lex) # an unknown option letter
