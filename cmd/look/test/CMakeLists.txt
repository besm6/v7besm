# look's b6sim cases -- task C5b.
#
# EVERY CASE NAMES ITS DICTIONARY, and that is not a style choice -- it is ../../README.md §9
# applied to the sharpest instance of its own rule.  look's default file is the absolute path
# /usr/dict/words, and under b6sim the system calls are the HOST's, so that path is the BUILD
# MACHINE's: on macOS it does not exist at all (the list is at /usr/share/dict/words), and on
# a Linux host it would be a 25,000-word file nothing here chose.  A case that read it would
# assert something about whoever ran the build.  So the bare `look word' form -- which is the
# form look mostly exists in -- is asserted under the booted kernel, in kernel/test/filters,
# and it is the reason the dictionary is on the image at all.
#
# dict.txt IS THE STAGED DICTIONARY, copied.  It is a copy of ../words rather than a fixture
# of its own so that the cases below and kernel/test/filters interrogate the same list; if it
# drifted, the two harnesses would be answering about different data and the booted pass
# would stop being a second opinion on this one.
#
# THE EXPECTATIONS ARE grep's, NOT look's.  `look key file' prints every line whose
# canonicalised form has the canonicalised key as a prefix, so `LC_ALL=C grep ^key' is an
# independent implementation of the same question -- a different program, on the host, over
# the same bytes.  That is worth more here than a hand count would be: the binary search
# below is the part of look that can be subtly wrong, and it can be wrong in a way that
# returns a plausible SUBSET.  A captured .expected would have asserted only that the search
# is deterministic.
#
# `fold' AND `dict' EXERCISE THE TWO ctype PATHS, which is where §11 bites: canon() calls
# isupper() and isalnum() on a byte the caller chose, against a 129-entry class table.  `utf8'
# and `utf8one' are the other side of the same rule -- a byte above 0177 is a word
# constituent with no case, so `го' finds three Cyrillic entries and -d would not strip them.
#
# THE TWO-ARGUMENT FORM DOES NOT IMPLY -df, which is easy to misread from the source: v7 sets
# fold and dict only when there is NO file argument.  So `fold' has to pass -f explicitly,
# and a port that moved the defaults would fail `find' rather than `fold'.

set(LOOK_FIXTURES dict.txt)
set(_copied)
foreach(f ${LOOK_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/look/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(look_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests look_fixtures)
endif()

b6_progtest(look find)    # one exact entry, found by the binary search
b6_progtest(look prefix)  # a prefix, and every entry under it
b6_progtest(look missing) # a key past the end of the list: no output, status 0
b6_progtest(look fold)    # -f, and isupper() on a byte the caller chose
b6_progtest(look dict)    # -d, and isalnum() on the same
b6_progtest(look utf8)    # a Cyrillic prefix: bytes above 0177 are word constituents
b6_progtest(look utf8one) # ... and one exact Cyrillic entry
b6_progtest(look noent)   # a dictionary that is not there
