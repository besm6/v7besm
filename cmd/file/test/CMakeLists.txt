# b6sim cases for /bin/file -- task C5f.  ../../README.md §9 is the harness; the files are
# <case>.args and <case>.expected, and run-prog-test.sh diffs stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule), and for this program the rule bites hardest: file(1) prints the name it was given
# in front of every answer, so EVERY case would carry an absolute build path.
#
# FIVE OF THE FIXTURES ARE BUILD ARTEFACTS AND ARE GENERATED HERE, because the whole subject
# of this port is what a BESM-6 a.out looks like and a checked-in one would be a copy of the
# thing under test that nothing keeps in step:
#
#   execbin   ${B6_ROOTFS}/bin/file itself -- FMAGIC with RELFLG set and a symbol table
#   stripbin  ... the same, through b6strip, so `not stripped' has to disappear
#   purebin   ${B6_ROOTFS}/bin/sh -- the only NMAGIC program on the root image; the
#                 other is /mnt/test/puret, on the test pack (../../../scripts/test.manifest)
#   objbin    crt0.o -- FMAGIC WITHOUT RELFLG, which is the distinction v7's file could
#                 not draw at all
#   archbin   libc.a -- ARMAG, whose value is v7's and whose width is not
#
# AND NONE OF THE FIXTURE NAMES ENDS IN `.out'.  run-prog-test.sh captures the program's
# standard output as `<case>.out' IN THE WORKING DIRECTORY, so a fixture named exe.out for
# case `exe' is truncated by the redirection before the program ever opens it -- and file(1)
# then reports it as `empty', which is a plausible answer and passes nothing.  It cost half
# an hour here.  ../../README.md §9 records it as the harness's sixth limit.
#
# THE DIVERGENCE IS ASSERTED AND IT IS SHARP.  cyr and rustext are the two cases that say
# well-formed UTF-8 is text; badbyte is the negative that says malformed bytes are still
# garbage.  Restoring v7's `buf[i] & 0200' test, rebuilding and re-running gives
# `rus.txt: data' and `cyr.c: c program text with garbage' -- so a half-undone divergence
# fails rather than passing quietly (C5d's rule).
#
# HOST file(1) IS NOT AN ORACLE and cannot be: its whole subject is the magic numbers of the
# machine it runs on.  It calls execbin `data' and archbin `data', which is exactly the gap
# ../../doc/File_Magic.md exists to close on the host side.

set(FILE_FIXTURES csrc.c cyr.c badbyte.c asrc.s fsrc.f page.1 rus.txt english.txt
                  plain.txt empty.txt bin.dat list.txt)
set(_copied)
foreach(fx ${FILE_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${fx} ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fx}
        COMMENT "cmd/file/test: ${fx}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${fx})
endforeach()

# The binary fixtures, under fixed names so a checked-in .expected can name them.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/execbin
    COMMAND ${CMAKE_COMMAND} -E copy ${B6_ROOTFS}/bin/file ${CMAKE_CURRENT_BINARY_DIR}/execbin
    DEPENDS b6prog_file COMMENT "cmd/file/test: execbin" VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stripbin
    COMMAND ${CMAKE_COMMAND} -E copy ${B6_ROOTFS}/bin/file
            ${CMAKE_CURRENT_BINARY_DIR}/stripbin
    COMMAND ${B6STRIP} ${CMAKE_CURRENT_BINARY_DIR}/stripbin
    DEPENDS b6prog_file COMMENT "cmd/file/test: stripbin" VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/purebin
    COMMAND ${CMAKE_COMMAND} -E copy ${B6_ROOTFS}/bin/sh ${CMAKE_CURRENT_BINARY_DIR}/purebin
    DEPENDS b6prog_sh COMMENT "cmd/file/test: purebin" VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/objbin
    COMMAND ${CMAKE_COMMAND} -E copy ${B6_LIBC_DIR}/crt0.o ${CMAKE_CURRENT_BINARY_DIR}/objbin
    DEPENDS libc COMMENT "cmd/file/test: objbin" VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/archbin
    COMMAND ${CMAKE_COMMAND} -E copy ${B6_LIBC_DIR}/libc.a ${CMAKE_CURRENT_BINARY_DIR}/archbin
    DEPENDS libc COMMENT "cmd/file/test: archbin" VERBATIM)
list(APPEND _copied
    ${CMAKE_CURRENT_BINARY_DIR}/execbin ${CMAKE_CURRENT_BINARY_DIR}/stripbin
    ${CMAKE_CURRENT_BINARY_DIR}/purebin ${CMAKE_CURRENT_BINARY_DIR}/objbin
    ${CMAKE_CURRENT_BINARY_DIR}/archbin)

add_custom_target(file_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests file_fixtures)
endif()

b6_progtest(file exe)         # FMAGIC + RELFLG + a symbol table
b6_progtest(file stripped)    # ... and the same file with the symbol table gone.  a_syms is
                              # word 5 here and was word 4 on a PDP-11
b6_progtest(file pure)        # NMAGIC
b6_progtest(file object)      # FMAGIC WITHOUT RELFLG: a distinction v7 could not draw
b6_progtest(file archive)     # ARMAG, one 48-bit word rather than two bytes
b6_progtest(file csource)     # C source, found through the comment skipper
b6_progtest(file asmsource)   # b6as source: `.text', `.globl' and a Madlen mnemonic, where
                              # v7's tables held PDP-11 pseudo-ops and PDP-11 mnemonics
b6_progtest(file fortran)     # the fortran keyword table, left as v7 wrote it
b6_progtest(file roff)        # a manual page
b6_progtest(file english)     # english()'s vowel and letter-frequency test
b6_progtest(file ascii)       # ... and text that fails it
b6_progtest(file rustext)     # THE DIVERGENCE: well-formed UTF-8 is text.  v7: `data'
b6_progtest(file cyr)         # ... and a C source with a Cyrillic identifier is still C
                              # source.  v7: `c program text with garbage'
b6_progtest(file badbyte)     # THE NEGATIVE: 0377 0376 is not UTF-8, so it IS garbage
b6_progtest(file binary)      # a file of control bytes and invalid sequences
b6_progtest(file emptyfile)   # nothing at all
b6_progtest(file dir)         # a directory
b6_progtest(file noent)       # a file that is not there
b6_progtest(file several)     # more than one argument in one run
b6_progtest(file listfile)    # -f: the names come from a file
