# b6sim cases for /bin/join -- task C5f.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.in, <case>.expected and <case>.status, and run-prog-test.sh diffs
# stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): join prints the name it was given in `join: can't open nosuch.txt'.
#
# HOST join IS THE ORACLE FOR TEN OF THE EIGHTEEN (C5e's rule, the cheap third opinion), and
# it agrees byte for byte with all ten -- including the many-to-many rewind, which is most of
# the program, and two of the upstream bugs fixed below.  Six of the other eight are outside
# its dialect rather than in disagreement with it: BSD join spells the join field `-1'/`-2'
# where v7 spells it `-j1'/`-j2'/`-j', takes no bare `-a', wants its -o list as one
# comma-separated argument, and words its diagnostics its own way.  THE LAST TWO ARE THE TWO
# DIAGNOSTICS THIS PORT ADDED, which is the result to want: they are the cases where the host
# does something and this program refuses to.
#
# WHAT ONLY THIS SUITE ASSERTS is the four things v7 got wrong, and three of them are only
# reachable from a badly-shaped input:
#
#   toomany     -- 25 fields against NFLD 20.  v7 ran out of ppi[0] into ppi[1] and past it.
#   nonewline   -- a last line with no newline, which v7's own comment says it "fails badly"
#                  on: the field loop stepped over the terminator and read the rest of the
#                  3072-byte buffer.
#   shortline   -- a line with fewer fields than the join field, which handed NULL to
#                  strcmp() and to printf("%s").
#   stdin2      -- `-' as FILE 2, which cannot work: the many-to-many path fseek()s it.
#                  v7 accepted it and produced a wrong answer.

set(JOIN_FIXTURES a.txt b.txt c.txt d.txt e.txt f.txt short.txt nonl.txt
                  utf8a.txt utf8b.txt wide.txt one.txt)
set(_copied)
foreach(fx ${JOIN_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${fx} ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fx}
        COMMENT "cmd/join/test: ${fx}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${fx})
endforeach()
add_custom_target(join_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests join_fixtures)
endif()

b6_progtest(join plain)       # the ordinary join, and one key repeated in file 2
b6_progtest(join stdin)       # `-' names standard input, which is legal for FILE 1
b6_progtest(join a1)          # -a1: unpairable lines of file 1 as well
b6_progtest(join a2)          # -a2
b6_progtest(join aboth)       # -a with no number: both
b6_progtest(join olist)       # -o 1.1 2.2 1.3, v7's spelling of the output list
b6_progtest(join empty)       # -e with -a -o: what a missing field prints as
b6_progtest(join manymany)    # KEYS REPEATED ON BOTH SIDES -- the ftell/fseek rewind, which
                              # is most of this program and the reason file 2 must be a file
b6_progtest(join tsep)        # -t:
b6_progtest(join j2)          # -j2 1: the join field of file 2 alone
b6_progtest(join jboth)       # -j 1: both at once
b6_progtest(join shortline)   # a line with FEWER FIELDS than the join field: v7 handed the
                              # terminating NULL to strcmp() and to printf("%s")
b6_progtest(join nonewline)   # a last line with no newline, which v7 read past
b6_progtest(join utf8)        # §11: Cyrillic keys, compared and printed as bytes
b6_progtest(join toomany)     # 25 fields against NFLD 20, which v7 enforced nowhere
b6_progtest(join stdin2)      # `-' as FILE 2 is refused, the rewind needing a real file
b6_progtest(join usage)       # one file name
b6_progtest(join noent)       # a file that is not there
