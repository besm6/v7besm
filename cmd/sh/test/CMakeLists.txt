# Smoke tests for the ported /bin/sh, run under b6sim.
#
# b6sim runs one BESM-6 a.out and services its syscalls on the host, which is enough to
# drive the shell's lexer, parser, macro expansion, name tree, control flow and
# built-ins -- and, underneath all of them, the arena and the expression stack, which is
# where this port's BESM-6 pointer work lives.  It is NOT enough to reach fork, exec,
# pipes, file redirection, globbing or traps; ../README.md says why, one by one.  Those
# need the real kernel under SIMH, where the shell now runs but no test drives them yet.
#
# The fixtures are commented with `#'.  That needed word.c to grow a comment character
# first: v7's shell had none, and the stand-in -- a `:' line -- has its words really
# parsed, so a backquote in one runs to end of file.  See ../README.md.

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

# fork and exec of another SHELL SCRIPT, which is how a v7 system runs one at all: there
# is no `#!' here -- neither the kernel's getxfile() nor b6sim implements it, because v7
# had no such thing, and to this shell such a line is simply a comment -- so exec comes
# back ENOEXEC and the shell reads the file itself (the ENOEXEC case in service.c).
# Covers arguments, PATH search, exit status and a child that forks in turn.
b6_shtest(script)

# The comment character this port added to the lexer (word.c), which v7 had not got.
# Covers the cases where a `#' must NOT start one -- inside a word, inside either kind
# of quote, after a backslash, in $# and in a here-document body -- and the two ways the
# skip could be written wrongly: eating a `\'-newline as a continuation, and spinning on
# SHEOF at an unterminated last line, which is why the fixture has no final newline.
b6_shtest(comment)

# The one that guards the arena and the break.  Without chkstak() (stak.h) this script
# does not print `no space' -- it runs off the end of the break and out of the address
# space, because the memory fault v7 relied on to grow the arena mid-push is not
# something b6sim can deliver.
b6_shtest(nospace)
