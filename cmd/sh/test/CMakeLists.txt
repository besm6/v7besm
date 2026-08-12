# Smoke tests for the ported /bin/sh, run under b6sim.
#
# b6sim runs one BESM-6 a.out and services its syscalls on the host, which is enough to
# drive the shell's lexer, parser, macro expansion, name tree, control flow and
# built-ins -- and, underneath all of them, the arena and the expression stack, which is
# where this port's BESM-6 pointer work lives.  It reaches fork, exec and pipes too
# (SYS_fork, SYS_exece and SYS_pipe are real host calls in cmd/sim/syscall.cpp), which is
# what script.sh runs on; what it cannot give is a terminal, and with it the interactive
# paths, job control and traps.  Those need the real kernel under SIMH.
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

# Task C11: the shell carries eight bits.  v7 marked a quoted character by setting bit
# 0200 of it and trim() cleared that bit from every word on its way to argv, so a byte of
# UTF-8 came out as an ASCII letter; the mark is a QESC byte in front of the character now
# (defs.h).  Covers the four ways of quoting, substitution, splitting, `case', both kinds
# of here-document, command substitution, and the byte 0377 itself -- the one value the
# stored form has to write twice.  This is also the only fixture here that execs anything:
# `set' cannot show what reached ARGV, so run-sh-test.sh brings ./echo along.
b6_shtest(utf8)

# The one that guards the arena and the break.  Without chkstak() (stak.h) this script
# does not print `no space' -- it runs off the end of the break and out of the address
# space, because the memory fault v7 relied on to grow the arena mid-push is not
# something b6sim can deliver.
b6_shtest(nospace)

# Task C29, and the two ends of the MACHINE stack -- the other stack, the one the C
# calls run on, which nothing but the shell itself can check (../README.md, "The stack").
# nest guards the ceiling that the split of execute() bought: twelve levels of nesting,
# where the shell before it died at nine and died silently, having wrapped onto word 0
# and rewritten its own const image.  toodeep guards the diagnostic at the far end, an
# `eval' that evals itself for ever.
b6_shtest(nest)
b6_shtest(toodeep)

# And the limit C29 turned out to share a cause with: a pipeline of four stages inside a
# command substitution, which task C8 recorded as an undiagnosed `SIGNAL 4' and which is
# the same stack running out one frame lower down.
b6_shtest(subpipe)
