# Tests for /usr/bin/yacc, the native build of cmd/yacc (task C10c).  Two kinds:
#
#   rootfs_yacc_<case>  AGREEMENT.  Host b6yacc and native yacc over one grammar,
#                       diffed live.  Says the port is faithful, not that the
#                       parser is right -- two yaccs wrong the same way agree.
#   cmd_yacc_<case>     b6sim cases with a checked-in .expected: the diagnostics.
#
# The agreement cases carry label `rootfs', being of a kind with rootfs_yacc_size.
# b6yacc as a generator expression, never an installed one, which would be a
# different build of the same source and would say nothing.
function(yacc_agree name grammar)
    add_test(NAME rootfs_yacc_${name}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-yacc-test.sh
                $<TARGET_FILE:b6yacc> ${B6SIM} ${B6_ROOTFS}/usr/bin/yacc
                ${CMAKE_CURRENT_SOURCE_DIR}/../../yaccpar.c
                ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${grammar} ${ARGN})
    set_tests_properties(rootfs_yacc_${name} PROPERTIES LABELS rootfs)
endfunction()

# The six grammars C11-C17 will feed this yacc -- the set the besm6 size profile
# was measured against -- and the two this tree already generates.  Each is the
# real file: a grammar written for this suite could be sized to fit.
set(_cmd ${CMAKE_SOURCE_DIR}/cmd)
yacc_agree(expr   ${_cmd}/expr/expr.y)              # C11
yacc_agree(egrep  ${_cmd}/egrep/egrep.y)            # C26
yacc_agree(m4     ${_cmd}/m4/m4y.y)                 # C13
yacc_agree(make   ${_cmd}/make/gram.y)              # C14
yacc_agree(bc     ${_cmd}/bc/bc.y -d)               # C15/C16: peaks WSETSIZE
yacc_agree(awk    ${_cmd}/awk/awk.g.y -d)           # C17: peaks every other bound
yacc_agree(parser ${_cmd}/lex/parser.y -d)          # b6lex's own grammar
yacc_agree(calct  ${CMAKE_CURRENT_SOURCE_DIR}/../calct.y)
yacc_agree(calcu  ${CMAKE_CURRENT_SOURCE_DIR}/../calcu.y)  # the %union path

# ctest does not build; the staged program arrives through b6_progtest below.
if(TARGET build_tests)
    add_dependencies(build_tests b6yacc b6prog_yacc)
endif()

# run-prog-test.sh runs `env -i', so B6YACCPAR does not reach the guest and
# /usr/lib/yaccpar is not there: every case here fails before others() wants it.
b6_progtest(yacc noargs   DEST usr/bin/yacc) # no arguments -- same message as below
b6_progtest(yacc notfound DEST usr/bin/yacc) # an input file that is not there
b6_progtest(yacc badflag  DEST usr/bin/yacc) # an unknown option letter
b6_progtest(yacc ratfor   DEST usr/bin/yacc) # -r, the one option this port refuses
