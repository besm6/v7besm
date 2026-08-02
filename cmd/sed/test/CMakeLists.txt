# b6sim cases for /bin/sed -- task C5e.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.in, <case>.expected and <case>.status, and run-prog-test.sh diffs
# stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): sed prints the file name it was given -- `Can't open nosuch.txt', `Cannot open
# pattern-file: x' -- so a @srcdir@ path would put an absolute build path into a checked-in
# .expected.
#
# A SCRIPT CONTAINING A SPACE CANNOT BE AN ARGUMENT HERE.  run-prog-test.sh expands the
# .args line unquoted, so it is split on whitespace and there is no quoting (task C5c's
# finding, ../../README.md §9).  That rules out most of what anybody actually writes --
# `s/beta two/BETA TWO/' is one argument to a shell and three to this harness -- so every
# such script is a checked-in `.sed' fixture and reaches sed through -f.  The one place a
# quoted script CAN be written is ../../../kernel/test/filters.sh, under a real shell, and
# that is where the quoted forms live.
#
# WHERE THE EXPECTATIONS CAME FROM, and this is the part worth reading.  sed's output is
# ordinary readable text, so C5d's rule applies rather than od's: every expectation below
# was DESIGNED from sed.1 and the sources -- a small fixture with a known answer says WHICH
# rule broke, where a second implementation only says that something did.  They were then
# cross-checked against the HOST's sed as an independent second opinion, and the six cases
# it disagrees about are exactly the six it should:
#
#   * gflag and subP use -g and s///P, which are v7's and which no later sed has.
#   * list, listfold, listnl and listutf8 are the l command, where this port diverges on
#     purpose (three octal digits, \012 for an embedded newline, UTF-8 passed through).
#
# Every other case agrees byte for byte with an implementation that shares no line with
# this one, which is worth more than the next ten hand-written cases -- grep/README.md's
# closing finding, pointed at sed as ../TODO.md asked.

set(SED_FIXTURES
    # the corpora
    text.txt nums.txt two.txt ctrl.txt utf8.txt long.txt half.txt toolong.txt
    # scripts that carry a space, a newline or a text argument
    space.sed hashn.sed append.sed insert.sed change.sed nextcmd.sed dcmd.sed
    pcmd.sed flagP.sed branch.sed braces.sed readf.sed exch.sed bend.sed nlist.sed
    # scripts that sit on one side or the other of a stated ceiling
    cmd99.sed cmd100.sed lab49.sed lab50.sed lab8.sed lab9.sed brace20.sed
    brace21.sed file10.sed file11.sed longname.sed relong.sed deep.sed bigrhs.sed)
set(_copied)
foreach(f ${SED_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/sed/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(sed_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests sed_fixtures)
endif()

# ---- the ordinary business ----------------------------------------------------------
b6_progtest(sed plain)          # s/// over a named file
b6_progtest(sed stdin)          # ... and off standard input, with no file argument
b6_progtest(sed twofiles)       # two inputs, one continuous line numbering
b6_progtest(sed nflag)          # -n, so only p speaks
b6_progtest(sed lastline)       # $, which is dolflag and only gline() can set it
b6_progtest(sed range)          # a two-number range
b6_progtest(sed rerange)        # a range between two expressions -- the inar state machine
b6_progtest(sed negate)         # !
b6_progtest(sed delete)         # d over a range
b6_progtest(sed subglobal)      # s///g
b6_progtest(sed subprint)       # s///p
b6_progtest(sed altdelim)       # any character may be the delimiter, here a comma
b6_progtest(sed emptyre)        # // reuses the last expression compiled
b6_progtest(sed backref)        # \(...\) and \1 \2 in the replacement
b6_progtest(sed ampersand)      # & is the matched text
b6_progtest(sed escamp)         # \& is a literal one -- the QESC decode, on the ASCII side
b6_progtest(sed anchor)         # ^...$
b6_progtest(sed dollar)         # $ at the end of an expression
b6_progtest(sed dot)            # .
b6_progtest(sed star)           # *
b6_progtest(sed class)          # [...]
b6_progtest(sed negclass)       # [^...]
b6_progtest(sed crange)         # [a-c], the range arm of the class compiler
b6_progtest(sed equals)         # =, whose whole output is a number
b6_progtest(sed equalsall)      # ... on every line
b6_progtest(sed quit)           # q
b6_progtest(sed semicolon)      # two commands on one line
b6_progtest(sed twoscripts)     # two -e options accumulate
b6_progtest(sed gflag)          # -g, which sed.1 never mentioned and now does
b6_progtest(sed ycmd)           # y/// over ASCII

# ---- the eighth bit.  Four of these are SHARP -- utf8class, utf8range, utf8rhs and utf8y --
# ---- and that was CHECKED rather than asserted: restore v7's two masks and two widths,
# ---- rebuild, and those four answers change.  C5d's rule is that a divergence whose test
# ---- would pass either way is not a test, and applying it here cost one rebuild and
# ---- corrected a claim that had already been written into three files (see utf8nomatch).
b6_progtest(sed utf8class)      # [пм] matches a Cyrillic line -- the feature
b6_progtest(sed utf8nomatch)    # a Cyrillic class does not match an unrelated ASCII line.
                                # NOT sharp, and the note is worth more than the case:
                                # grep's utf8nomatch IS sharp and this one was expected to
                                # be, on the same reasoning.  It is not.  See README.md SS2
b6_progtest(sed utf8range)      # SHARP, and the sharp NEGATIVE this suite really has:
                                # [а-я] over Cyrillic.  v7 masks the matched byte with 0177,
                                # so no byte of a Cyrillic letter can be in any class and
                                # the answer is silently empty
b6_progtest(sed utf8lit)        # a Cyrillic literal in the pattern
b6_progtest(sed utf8rhs)        # SHARP: s/x/привет/.  v7 stripped bit 0200 from every
                                # replacement byte that was not an escape, so this printed
                                # ten bytes of plausible ASCII
b6_progtest(sed utf8y)          # SHARP: y/мир/МИР/.  v7 masked the y table's source bytes
                                # AND indexed a 128-entry table with an unmasked one
b6_progtest(sed utf8amp)        # & carrying Cyrillic back into the line

# ---- scripts that need a space or a newline, so they arrive through -f ----------------
b6_progtest(sed space)          # the shape no .args line can hold: a pattern with a space
b6_progtest(sed hashn)          # `#n' as the first line of a script is -n
b6_progtest(sed append)         # a\ .  text() is the routine, and it had no bound
b6_progtest(sed insert)         # i\
b6_progtest(sed change)         # c\ over a range: the text goes at the END of it
b6_progtest(sed nextn)          # N, the embedded newline
b6_progtest(sed dcmd)           # N;P;D -- and v7 DISCARDS the pattern space when N hits
                                # end of input, so line8 never appears.  Pinned because it
                                # is a real difference from every later sed
b6_progtest(sed pcmd)           # P, up to the first newline
b6_progtest(sed subP)           # s///P, v7's undocumented flag, now in the page
b6_progtest(sed branch)         # : b t -- the label chain and dechain()
b6_progtest(sed braces)         # { } , which share BCOM with b and invert negfl
b6_progtest(sed readfile)       # r, whose text arrives through arout() after the line
b6_progtest(sed holdspace)      # h and G: the hold space
b6_progtest(sed bend)           # a b with no label branches to the end of the script

# ---- the l command, which is the fifth divergence ------------------------------------
b6_progtest(sed list)           # \177 and \001 in THREE octal digits: two cannot spell 0177
b6_progtest(sed listutf8)       # a byte above 0177 is passed through, as ed(1)'s l does
b6_progtest(sed listnl)         # an embedded newline is \012, where v7 printed it RAW and
                                # so made the one command whose job is an unambiguous
                                # listing produce an ambiguous one
b6_progtest(sed listfold)       # the 72-column fold, which is a fold of BYTES

# ---- diagnostics ---------------------------------------------------------------------
b6_progtest(sed usage)          # no arguments at all.  v7 exited 0 in silence
b6_progtest(sed badflag)        # v7 wrote this one to STDOUT, into the edited stream
b6_progtest(sed badcmd)
b6_progtest(sed unterm)         # s/a with no closing delimiter.  v7 treated the NUL as a
                                # no-op and compiled off the end of linebuf
b6_progtest(sed badbackref)     # \1 with no \( \)
b6_progtest(sed unbalanced)     # \( with no \)
b6_progtest(sed noent)          # an input file that is not there.  v7 exited 0
b6_progtest(sed deep)           # the recursion ceiling: a diagnostic, not a fault and not
                                # the empty answer the region past it gives

# ---- every stated ceiling, bracketed FROM BOTH SIDES.  fgrep's lesson in task C5c: a
# ---- test that asserts a diagnostic asserts the string and not the cause.
b6_progtest(sed maxcmds)        # 99 commands, accepted
b6_progtest(sed toomanycmds)    # 100, refused
b6_progtest(sed maxlabels)      # 49 labels, accepted
b6_progtest(sed toomanylabels)  # 50, refused
b6_progtest(sed maxlabellen)    # an eight-character label, accepted -- v7 refused it, its
                                # bound being one place tighter than its own asc[9]
b6_progtest(sed labeltoolong)   # nine, refused
b6_progtest(sed maxbraces)      # 20 nested { }, accepted
b6_progtest(sed toomanybraces)  # 21, refused -- and refused where it happens rather than
                                # at the end of the script, which is where v7 noticed
b6_progtest(sed maxbra)         # nine \( \) groups and \9, accepted
b6_progtest(sed toomanybra)     # ten, refused
b6_progtest(sed maxfiles)       # ten w files -- what sed.1 promises.  v7 allowed nine
b6_progtest(sed toomanyfiles)   # eleven, refused
b6_progtest(sed longname)       # a wfile name past the 40-byte row it is copied into.  v7
                                # had no bound here at all and walked into fcode[]
b6_progtest(sed relong)         # an expression past ESIZE
b6_progtest(sed scriptlong)     # a SCRIPT LINE past LBSIZE, which is what a 4,100-byte
                                # replacement really trips first -- v7 had no bound
b6_progtest(sed toolongin)      # an INPUT line past LBSIZE.  v7 dropped the tail in silence
b6_progtest(sed toolongout)     # ... and an output line grown past it by s///g, which v7
                                # diagnosed three times and then went on writing anyway.
                                # The pair is the point: one limit, both paths, loud

# The FILE oracle: what `w', `s///w' and a wfile truncated at COMPILE time do is not
# visible on standard output at all.  See run-sed-test.sh.
if(B6SIM)
    add_test(NAME cmd_sed_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-sed-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/sed ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_sed_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_sed)
        add_dependencies(build_tests b6prog_sed)
    endif()
endif()
