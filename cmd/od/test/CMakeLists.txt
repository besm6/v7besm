# od's b6sim cases -- task C5b, and the most careful set in it.
#
# EVERY .expected HERE CAME FROM A SECOND IMPLEMENTATION, in Python, on the host: the same
# packing, the same field widths, the same `*' suppression, written from od.1 rather than from
# od.c.  That is worth more for this program than for any other in the task, and the reason is
# in od.c's header: v7's putn() recursed exactly as deep as the field was wide and DISCARDED
# every digit above it, so a width that is wrong by one prints a number that is wrong by a
# factor of eight and looks entirely plausible.  A captured .expected would have asserted that
# od agrees with itself.  Sixteen octal digits of a 48-bit word is not checkable by eye.
#
# `word' IS THE SYNONYM ASSERTION and it is a diff of two identical files on purpose: -w and
# -o must produce the same bytes, because -w is a second spelling of -o and not a format of
# its own (../TODO.md asked for `a -w word dump in 16 octal digits'; making it a separate
# format would have been the change that really redefined a v7 flag).  If somebody later gives
# -w a format of its own, this case is what says so.
#
# `align' IS THE ONE THAT PINS THE COLUMNS.  -ob selects a 16-column word format and a
# 23-column byte format at once, so the octal words are padded out to the byte group's width
# by pre() -- which is the whole purpose of the width numbers and the only case here where
# they do anything visible.  Every other case selects one format, where pre() emits nothing.
#
# `dup' IS THE `*' SUPPRESSION over a fixture built for it: three identical 12-byte lines and
# then a different one, so the run must collapse to one line, one `*', and no more -- and the
# ADDRESS after it must still count every byte that was skipped.  A suppression that lost
# track of the address would pass a test that only looked at the text.
#
# `offset' EXERCISES THE ARGUMENT THAT MOVES TWO THINGS: `+6' seeks, and the offset parser
# also sets the radix of the address column as a side effect (od.1 says so now; v7's page
# never did).
#
# `chars' AND `bytes' ARE THE FORMATS THAT DID NOT CHANGE, and they are here to say so: they
# are per-BYTE and a byte is a byte on both machines, so the only thing the port could have
# broken in them is the six-bytes-per-word grouping.  text.txt is 21 bytes -- deliberately not
# a multiple of 12 or of 6 -- so the last group is short and its zero padding shows.
#
# text.txt IS COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@, C5a's rule:
# `offset' names it on the command line, and `noent' asserts a diagnostic that quotes the name
# it was given.

set(OD_FIXTURES text.txt)
set(_copied)
foreach(f ${OD_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/od/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(od_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests od_fixtures)
endif()

b6_progtest(od octal)   # the default: the 48-bit word in 16 octal digits
b6_progtest(od word)    # -w is the same thing under the name that says so
b6_progtest(od decimal) # -d, 15 digits
b6_progtest(od hex)     # -x, 12 digits
b6_progtest(od chars)   # -c, per byte, six to a word
b6_progtest(od bytes)   # -b, per byte
b6_progtest(od align)   # -ob: two formats, and the padding that lines them up
b6_progtest(od dup)     # the `*' line, and the address it must go on counting through
b6_progtest(od offset)  # +6 seeks, and sets the address radix
b6_progtest(od noent)   # a file that is not there
