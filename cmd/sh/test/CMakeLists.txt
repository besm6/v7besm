# Smoke tests for the ported /bin/sh, run under b6sim.
#
# b6sim runs one BESM-6 a.out and services its syscalls on the host, which is enough to
# drive the shell's lexer, parser, macro expansion, name tree, control flow and
# built-ins -- and, underneath all of them, the arena and the expression stack, which is
# where this port's BESM-6 pointer work lives.  It is NOT enough to reach fork, exec,
# pipes, file redirection, globbing or traps; ../README.md says why, one by one.  Those
# wait for the real kernel under SIMH (kernel/TODO.md, task 25).

# The shell b6_prog() staged.  Named through B6_ROOTFS rather than a relative path,
# since that is the variable that decides where it went.
set(SH ${B6_ROOTFS}/bin/sh)

macro(b6_shtest name)
    if(B6SIM)
        add_test(NAME sh_${name}
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-sh-test.sh
                    ${B6SIM} ${CMAKE_CURRENT_SOURCE_DIR} ${SH} ${name}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        # Both labels: `rootfs' is the staged-program family this belongs to, and `sh'
        # runs these two alone.
        set_tests_properties(sh_${name} PROPERTIES LABELS "rootfs;sh")
    endif()
endmacro()

b6_shtest(smoke)
b6_shtest(syntax)

# Here-documents, and with them the only file redirection b6sim can reach: a built-in
# cannot be redirected, but a ( ) SUBSHELL can, and the built-ins inside it still print.
# Covers subst() and copy(), including a document longer than CPYSIZ so that the flush
# boundary is crossed.
b6_shtest(heredoc)

# The one that guards the arena and the break.  Without chkstak() (stak.h) this script
# does not print `no space' -- it runs off the end of the break and out of the address
# space, because the memory fault v7 relied on to grow the arena mid-push is not
# something b6sim can deliver.
b6_shtest(nospace)
