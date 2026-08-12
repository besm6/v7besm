# tail's b6sim cases -- task C5b.
#
# `blocks' IS THE CASE THE PORT EXISTS TO SETTLE, and it is built so that it can only pass
# under one answer.  ../README.md left `-b's unit open -- v7's `n <<= 9' is 512 -- and this port
# takes BSIZE (3072), for dd(1)'s reason: a constant is the user's business only while it
# still names something on this machine, and 512 named a PDP-11 disk block (../README.md §4).
# big.txt is 3072 bytes of `x' followed by the nine bytes `boundary\n', so `tail +2b' skips
# exactly one filesystem block and prints the marker.  Under v7's 512 it would skip 512 and
# print 2569 bytes of `x' first.  The whole assertion is nine bytes wide.
#
# `reverse' IS THE ONE THAT EXERCISES THE RING, which is the part of this program a port can
# most easily break in a way the other cases would not see: -r walks bin[] BACKWARDS from the
# write cursor, wrapping through index 0, and emits each line as it finds it.  `lines' walks
# the same loop and prints from one write, so it would pass with the per-line emission broken.
#
# `chars' IS THE SEEKABLE PATH.  -5c on a seekable descriptor takes lseek(0,-5,2) and `goto
# copy' and never touches the ring at all -- a different route through the program than every
# other case here, and the one the piped/not-piped probe selects between.  What it cannot
# assert is the OTHER branch: standard input is a file under this harness, always, so the
# read-and-discard path taken over a pipe belongs to kernel/test/filters and is asserted
# there.
#
# `utf8' asserts nothing subtle and is here for what it does not do: lines are found by the
# byte 012, which cannot occur inside a UTF-8 sequence, so tail is byte-transparent by
# construction and would only stop being so if somebody made it inspect a character.
#
# big.txt IS COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@, C5a's rule:
# `blocks' names it on the command line, and `noent' asserts a diagnostic that quotes the name
# it was given.
#
# Every .expected here was computed from the fixtures rather than captured: the line ranges
# by hand, `boundary\n' from the way big.txt was built, and `chars' by counting bytes back
# from the end of an 81-byte file.

set(TAIL_FIXTURES big.txt)
set(_copied)
foreach(f ${TAIL_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/tail/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(tail_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests tail_fixtures)
endif()

b6_progtest(tail default)   # no argument at all is `-10l'
b6_progtest(tail lines)     # -3
b6_progtest(tail fromstart) # +5 counts from the beginning
b6_progtest(tail chars)     # -5c, and the seekable path it takes
b6_progtest(tail reverse)   # -3r, the ring walked backwards
b6_progtest(tail blocks)    # +2b: one block is BSIZE here, not 512 (see above)
b6_progtest(tail utf8)      # lines are 012-delimited, so Cyrillic is carried whole
b6_progtest(tail usage)     # an unknown type letter
b6_progtest(tail noent)     # a file that is not there
