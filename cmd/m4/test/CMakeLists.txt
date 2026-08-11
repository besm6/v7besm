# m4(1) under b6sim.  105 cases, and THE FIRST SUITE HERE WHOSE INPUT LANGUAGE IS NOT THE
# ARGUMENT LIST: m4 with no arguments reads standard input, so nearly every case is a
# <case>.in fed VERBATIM.  The no-quoting limit that shaped cmd/expr/test and
# cmd/egrep/test -- ../../../scripts/run-prog-test.sh splits .args on whitespace and there
# is no way to write a quoted or an empty argument -- therefore binds on almost nothing
# here; only the four cases that name files on the command line touch it.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than reached through @srcdir@:
# a file name for include() lives inside the .in, which gets no substitution, and the
# harness runs with this directory as its working directory.
#
# WHAT HAS NO CASE, and why, is in ../README.md under "What this harness cannot say".  The
# short version: syscmd(), because b6sim's exec loads a BESM-6 a.out and the host's
# /bin/sh is not one, so a case would pass while asserting nothing.  It is checked by hand
# under the booted kernel instead, and the transcript is in that section.
set(M4_FIXTURES hello.m4 hello2.m4 inc1.m4 inc2.m4 self.m4)
set(_copied)
foreach(f ${M4_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/m4/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(m4_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests m4_fixtures)
endif()

# Input, files, and the eight-bit path.
b6_progtest(m4 stdin)          # no macros at all: the program is a filter first
b6_progtest(m4 cmdfile)        # a named file
b6_progtest(m4 twofiles)       # two, in order
b6_progtest(m4 dashstdin)      # `-' is standard input, and only a bare `-'
b6_progtest(m4 nofile)         # the diagnostic, and status 1
b6_progtest(m4 eofname)        # input ending in a macro call with NO trailing newline:
                               # v7 read type[EOF] and pushed a literal 0377 into the output
b6_progtest(m4 nulbyte)        # a NUL is an ordinary byte; v7 ended the file on one
b6_progtest(m4 byte377)        # and so is 0377

# The scanner.
b6_progtest(m4 quote)
b6_progtest(m4 quotenest)      # nested quotes: only the outer pair is stripped
b6_progtest(m4 quoteeof)
b6_progtest(m4 comment)
b6_progtest(m4 commentarg)     # a comment inside an argument is dropped, newline kept
b6_progtest(m4 noparen)        # a name not followed by `(' is called with no arguments
b6_progtest(m4 spaceparen)     # the `(' must IMMEDIATELY follow, as the page says
b6_progtest(m4 leadingblank)   # leading blanks in an argument are skipped, trailing kept
b6_progtest(m4 longword)       # a 200-byte word passes through: v7 ran off token[128]
b6_progtest(m4 unexpectedeof)

# define, undefine, and $n.
b6_progtest(m4 define)
b6_progtest(m4 definenull)
b6_progtest(m4 defineself)
b6_progtest(m4 dollarargs)
b6_progtest(m4 dollarmissing)  # a missing argument is the null string
b6_progtest(m4 dollarnondigit) # `$x' and a trailing `$' -- the two putbak arms
b6_progtest(m4 redefine)
b6_progtest(m4 nested)
b6_progtest(m4 undefine)
b6_progtest(m4 undefnone)
b6_progtest(m4 redefbuiltin)   # THE CASE THIS PORT'S ONE DESIGN DECISION EXISTS FOR:
                               # define(`len',X) must make len mean X.  With v7's
                               # builtin-is-a-malloc-address it printed 3.
b6_progtest(m4 undefbuiltin)   # and undefining one leaves ordinary text
b6_progtest(m4 undefalias)     # the sharp half: the freed block must not make an
                               # unrelated macro INTO the builtin.  v7 printed 2.

# The other builtins.
b6_progtest(m4 ifdefyes)
b6_progtest(m4 ifdefno)
b6_progtest(m4 ifdefnothird)
b6_progtest(m4 ifdefunix)      # `unix' is predefined, so b6cpp's -Dunix reached the guest
b6_progtest(m4 changequote)
b6_progtest(m4 changequoteone)
b6_progtest(m4 changequotereset)
b6_progtest(m4 changequoteempty) # an empty argument restores the default: with NUL now an
                                 # ordinary byte, v7's would have made every NUL a quote
b6_progtest(m4 divert)
b6_progtest(m4 divertorder)    # the streams concatenate in numerical order at exit
b6_progtest(m4 divertdiscard)  # a stream outside 0-9 is discarded
b6_progtest(m4 divnum)
b6_progtest(m4 undivertone)
b6_progtest(m4 undivertall)
b6_progtest(m4 undivertnested) # undivert into another diversion
b6_progtest(m4 divertnul)      # a NUL survives the temp file: v7's copy stopped at it
b6_progtest(m4 dnl)
b6_progtest(m4 ifelse3)
b6_progtest(m4 ifelse4)
b6_progtest(m4 ifelsechain)
b6_progtest(m4 ifelseshort)
b6_progtest(m4 incr)
b6_progtest(m4 incrneg)
b6_progtest(m4 incrnonnum)
b6_progtest(m4 len)
b6_progtest(m4 lennoarg)       # no argument: v7 measured a stale argstk slot
b6_progtest(m4 index)
b6_progtest(m4 indexnone)
b6_progtest(m4 indexprefix)    # index(ab,abc): v7 kept comparing past ab's NUL
b6_progtest(m4 substr)
b6_progtest(m4 substrnolen)    # a missing length reaches the END of the string; v7 used
                               # TOKS, so it truncated anything past 128 bytes
b6_progtest(m4 substrneg)
b6_progtest(m4 substrpast)
b6_progtest(m4 translit3)
b6_progtest(m4 translit2)      # the two-argument form deletes
b6_progtest(m4 translitshort)
b6_progtest(m4 include)
b6_progtest(m4 includenest)
b6_progtest(m4 includenone)
b6_progtest(m4 sinclude)
b6_progtest(m4 sincludefound)
b6_progtest(m4 includepending) # a failed sinclude must not swallow what was pending:
                               # v7 moved the pushback floor before the open and never
                               # moved it back, so `tail' was lost for ever
b6_progtest(m4 includedeep)    # a file that includes itself, bounded
b6_progtest(m4 maketemp)       # the NAME carries the pid, so only its shape is assertable
b6_progtest(m4 errprint)
b6_progtest(m4 errprintpct)    # printed literally: v7 handed user text to printf as a
                               # FORMAT, with five argstk slots it had not checked
b6_progtest(m4 dumpdef)        # two named macros, not the whole table, which would churn
b6_progtest(m4 shift)          # still refused, which is v7's own state.  Status 0.

# eval -- the grammar.
b6_progtest(m4 evaladd)
b6_progtest(m4 evalprec)
b6_progtest(m4 evalparen)
b6_progtest(m4 evalunary)
b6_progtest(m4 evalpower)
b6_progtest(m4 evalrel)        # all six relationals
b6_progtest(m4 evallogic)
b6_progtest(m4 evalneg)
b6_progtest(m4 evalempty)      # the grammar's empty production
b6_progtest(m4 evalbig)        # 10^12 -- past 32 bits, and the word holds it
b6_progtest(m4 evalbad)
b6_progtest(m4 evaldivzero)    # diagnosed: the divide instruction FAULTS on this machine
b6_progtest(m4 evalmodzero)

# Eight bits, positive and negative.  The negative controls are the sharp ones: with v7's
# 128-entry table a Cyrillic byte indexed past the array, and the failure was a plausible
# wrong answer -- a word quietly eaten -- rather than an error.
b6_progtest(m4 utf8pass)       # negative control: Cyrillic prose comes out byte for byte
b6_progtest(m4 utf8name)       # positive: a macro name may be Russian
b6_progtest(m4 utf8undef)      # negative: being a "name" must not make a word vanish
b6_progtest(m4 utf8len)        # 12, not 6: len counts BYTES
b6_progtest(m4 utf8index)      # a byte offset
b6_progtest(m4 utf8substr)     # a byte-counted substring, which cuts a letter in half
b6_progtest(m4 utf8translit)   # transliteration is by byte, and this is what that looks like
b6_progtest(m4 utf8quote)
b6_progtest(m4 utf8divert)     # and through a diversion temp file and back

# The bounds.  Four diagnostics, all reachable, all status 1.  v7 checked two of them and
# checked one of those one past the end of its array.
b6_progtest(m4 argstack)
b6_progtest(m4 callstack)      # reachable only since argstk grew to 3 slots per frame
b6_progtest(m4 bigarg)
b6_progtest(m4 pushback)
