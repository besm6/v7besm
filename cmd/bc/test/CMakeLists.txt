# Two harnesses for bc(1) -- task C16 -- because bc is a compiler with an interpreter
# behind it, and the two halves fail in different ways.
#
# THE PLAIN CASES CHECK THE COMPILER.  b6_progtest runs the staged /bin/bc with -c over
# <case>.in and diffs the dc source that comes out against <case>.expected.  That output
# is what a regression in the bundle arena, in register allocation or in a grammar action
# changes first, and it is the only place a reader can see the translation at all.
#
# THE run.* CASES CHECK THE MEANING.  ./run-bc-dc.sh feeds that same dc source to the
# staged /bin/dc and diffs the NUMBERS, so a case says 3628800 rather than a line of
# postfix.  It is also the only way lib.b can be tested by a program rather than by eye:
# run.math and run.mathid name it through @rootfs@ in a .args file.
#
# WHAT NEITHER CAN SAY, and is checked by hand instead (README.md, "What this harness
# cannot say"): the pipe.  bc's default path forks and execs "/bin/dc", and b6sim resolves
# that against the HOST filesystem -- where the first execl finds nothing on a Mac and the
# second finds the host's own dc, so a case that left -c off would assert nothing at all.
# Nor the SIGINT that yyinit() ignores, nor the terminal prompt.
#
# TWO EXPECTATIONS HOLD BYTES ABOVE 0177 AND BELOW 040, and that is the point of them: a
# dc register name is one byte, functions being 01..032 and arrays 0241..0272 (dc's
# ARRAYST and above), so array.expected and define.expected carry the names themselves.
# They are the assertion that getf()/geta() still hand out what ../../dc/dc.h reads.
#
# EVERY CASE EXITS 0 BUT THREE.  End of input and `quit' both exit 0 here, where v7 exited
# 1 from the same path; the three .status files are the bounds -- longconst, nestwhile and
# nofile -- which diagnose and stop.

# The compiler: one case per family of grammar rules.
b6_progtest(bc arith)          # the six binary operators
b6_progtest(bc unary)          # `-e' is  0 e -  and the minus is not part of the number
b6_progtest(bc paren)
b6_progtest(bc constant)       # a leading or trailing `.', and A-F under ibase 16
b6_progtest(bc assign)         # `=' and the six `=op' forms
b6_progtest(bc incdec)         # ++ and -- , prefix and postfix
b6_progtest(bc array)          # ARRAY REGISTER NAMES: 0241 upward, raw, in .expected
b6_progtest(bc ifstat)
b6_progtest(bc whilestat)
b6_progtest(bc forstat)
b6_progtest(bc breakstat)
b6_progtest(bc breakout)       # `break' outside a loop: v7 read bstack[-1] to size it
b6_progtest(bc define)         # FUNCTION REGISTER NAMES: 01 upward, raw, in .expected
b6_progtest(bc defarray)       # an auto array: `0S' on an array register, ../../dc/README.md
b6_progtest(bc builtin)        # sqrt, length, scale
b6_progtest(bc bases)          # ibase, obase and scale as values and as targets
b6_progtest(bc qstr)           # A `%' IN A STRING: v7 read it as a printf conversion
b6_progtest(bc comment)
b6_progtest(bc syntax)         # yyerror's `c[...]pc' and the recovery that follows it
b6_progtest(bc quit)           # `quit' is obeyed when READ, so the line after it is gone
b6_progtest(bc nestif)         # ten deep: the register walk skips `[' and `a'
b6_progtest(bc nestwhile)      # eleven deep: v7 wrote past bstack[10] with no diagnostic
b6_progtest(bc longconst)      # v7 wrote past cary[1000] with no bound at all
b6_progtest(bc nofile)         # v7 diagnosed with ss unset and read through a null FILE *

# The meaning.  Same shape as b6_progtest -- <case>.in, <case>.expected, optional
# <case>.args and <case>.status -- but the dc source goes on to /bin/dc.
macro(b6_bctest name)
    if(B6SIM)
        add_test(NAME cmd_bc_${name}
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-bc-dc.sh
                    ${B6SIM} ${CMAKE_CURRENT_SOURCE_DIR} ${B6_ROOTFS} ${name}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(cmd_bc_${name} PROPERTIES LABELS cmd)
        # ctest does not build, so both programs and the library have to hang on the
        # target `make test' does build.
        if(TARGET build_tests)
            add_dependencies(build_tests b6prog_bc b6prog_dc rootfs_libb)
        endif()
    endif()
endmacro()

b6_bctest(run.numbers)
b6_bctest(run.func)
b6_bctest(run.recurse)         # 20! exactly, which is what unlimited precision is for
b6_bctest(run.array)           # an auto array across a call
b6_bctest(run.loop)
b6_bctest(run.break)
b6_bctest(run.scale)           # thirty places of 1/3 and of sqrt(2)
b6_bctest(run.bases)
b6_bctest(run.str)
b6_bctest(run.math)            # lib.b: s, c, e, l, a and j at the default scale of 20
b6_bctest(run.mathid)          # and the identities among them, to the last place scale keeps
