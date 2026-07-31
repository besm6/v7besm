# rev's b6sim cases -- task C5a, and the assertions for the one deliberate divergence in the
# phase.  ../README.md is the account; these are what hold it.
#
# `utf8' IS THE WHOLE POINT.  v7's rev reverses BYTES, which is the same thing as reversing
# characters on an ASCII machine and is not the same thing here: since task C11 this system's
# text is UTF-8 end to end (../../README.md §11), so byte reversal turns `привет мир' into
# twelve bytes of mojibake.  This rev reverses SEQUENCES, and the case requires `рим тевирп'
# -- which is a line a reader can check by eye, and which no byte-reversing rev can produce.
#
# `long' IS THE CASE THAT COSTS SOMETHING AND EARNS IT.  A line longer than the 1024-byte
# buffer is cut into pieces and each is reversed on its own, which is v7's behaviour and is
# kept -- but with characters rather than bytes the cut would fall through the middle of a
# two-byte letter unless it backs off to a boundary.  The fixture is 1023 bytes of `a'
# followed by `привет', so the 1024th byte is the LEAD BYTE of `п' and the incomplete
# sequence has to be carried into the next piece.  Get the back-off wrong and the first piece
# ends in half a letter; get it missing and the second piece begins with one.  Nothing
# shorter than the buffer can say any of that.
#
# `nonewline' IS AN UPSTREAM BUG.  v7's `case EOF: goto eof' abandoned whatever was already
# in the buffer, so a final line with no newline vanished: `printf abc | rev' printed nothing
# at all.  It prints `cba' now.
#
# EVERY .expected HERE WAS COMPUTED, NOT CAPTURED -- an independent implementation of the
# same walk, in Python, rather than a first run of the program under test.  For `ascii' and
# `utf8' the answer is checkable by eye anyway; for `long', 1042 bytes across three pieces,
# it is not, and capturing it would have asserted only that the program is deterministic.

set(REV_FIXTURES text.txt)
set(_copied)
foreach(f ${REV_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/rev/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(rev_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests rev_fixtures)
endif()

b6_progtest(rev ascii)     # the ordinary case, including an empty line
b6_progtest(rev file)      # a named file rather than standard input
b6_progtest(rev utf8)      # THE DIVERGENCE: sequences, not bytes
b6_progtest(rev nonewline) # the final line v7 threw away
b6_progtest(rev long)      # the buffer boundary, backed off to a character (see above)
b6_progtest(rev noent)     # a file that is not there aborts the run, as v7 had it
