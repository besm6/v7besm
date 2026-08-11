# Tests for make(1), task C14.
#
# THE MAKEFILES ARE READ FROM THE SOURCE DIRECTORY, `-f @srcdir@/<case>.mk', so
# almost nothing has to be copied here first: run-prog-test.sh substitutes
# @srcdir@ in a .args line, and a description file is data the program opens by
# name.  Only the archive fixture is built.
#
# EVERY CASE PASSES -n OR -q, so no case forks.  That is not timidity about the
# harness -- b6sim implements fork(2) and exec(2) -- but about the shell: dosys()
# hands a command line with metacharacters to /bin/sh, and the /bin/sh b6sim
# would reach is the build machine's, which is not a BESM-6 a.out.  What a
# command DOES is the transcript's to assert (../README.md).
#
# NOR CAN ANY CASE GLOB.  srchdir() reads a directory, and under b6sim the host's
# open() and fstat() succeed on one while read(2) refuses, so every directory
# looks empty and looks it exactly as an empty one does -- lib/test/progs.cmake
# says it at length for dirt.  Both the `*.c' expansion and the implicit-rule
# search that finds a source nobody named are image-only for that reason, and
# suffix.mk names its source in the description file so as not to need it.

# The archive lookarch() reads: one object, one global symbol, made with the
# in-tree host tools so the format cannot drift from what cmd/libaout writes.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libmk.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/libmk.a
    COMMAND ${B6CC_ENV} ${B6CC} -c ${CMAKE_CURRENT_SOURCE_DIR}/mkzork.c
            -o ${CMAKE_CURRENT_BINARY_DIR}/mkzork.o
    COMMAND ${B6AR} cr ${CMAKE_CURRENT_BINARY_DIR}/libmk.a
            ${CMAKE_CURRENT_BINARY_DIR}/mkzork.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mkzork.c
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "cmd/make/test: libmk.a" VERBATIM)

# The source files the descriptions name.  They have to be HERE and not in the
# source directory: a dependent's name is resolved against the program's working
# directory, which is this one, and exists() must have something to stat.
set(_copied ${CMAKE_CURRENT_BINARY_DIR}/libmk.a)
foreach(f a.c b.c hdr.h x.c)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/make/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()

add_custom_target(make_fixtures DEPENDS ${_copied})
if(TARGET build_tests)
    add_dependencies(build_tests make_fixtures)
endif()

# The graph, and what is printed while walking it
b6_progtest(make basic)      # dependents before the target, deepest first
b6_progtest(make printdesc)  # -p, the parsed description
b6_progtest(make debug)      # -d, and printdesc(YES) after it
b6_progtest(make dcolon)     # `::', one command line per rules line
b6_progtest(make default)    # .DEFAULT, and $< naming the target
b6_progtest(make suffix)     # a suffix rule, $< $@ $?
b6_progtest(make builtin)    # NO -r: the built-in rule table is read and parsed

# Macros
b6_progtest(make macros)     # $(A), ${A}, nesting, $$, $@, an unset name
b6_progtest(make cmdline)    # NAME=value in argv beats the description file
b6_progtest(make recurse)    # a macro defined in terms of itself

# Flags that answer rather than act
b6_progtest(make silent)     # .SILENT
b6_progtest(make keepgoing)  # -k carries on past a target it cannot make
b6_progtest(make stdin)      # -f -, the description file on standard input
b6_progtest(make touch)      # -t, as far as the message: -n keeps it idempotent
b6_progtest(make qok)        # -q on a target that is up to date: status 0, silent
b6_progtest(make qstale)     # -q on one that is not: status -1, and still silent

# Archive members: lookarch() over cmd/libaout.  ../README.md, "Archives"
b6_progtest(make armember)   # lib.a(member) resolves to the member's date
b6_progtest(make arentry)    # lib.a((entry)) resolves through the symbol table
b6_progtest(make armissing)  # and a member that is not there does not

# Diagnostics
b6_progtest(make nonames)    # an empty description file leaves nothing to make
b6_progtest(make nodescf)    # -f names a file that is not there
b6_progtest(make badflag)    # an unknown option letter
b6_progtest(make nosep)      # a rules line with no `:' or `::'
b6_progtest(make unknown)    # a dependent with no rule and no source
b6_progtest(make toodeep)    # the recursion bound, in place of a stack fault

# Eight bits: a target, a dependent and a command in Cyrillic.  funny[] is
# indexed by a char and a char is unsigned here, so the table is 256 wide and
# not v7's 128 -- ../README.md SS11's shape.
b6_progtest(make eightbit)
