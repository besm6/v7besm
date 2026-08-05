#
# man's b6sim cases -- task C25b.
#
# EVERY CASE NAMES ITS MANUAL TREE with -M, and that is look(1)'s rule in its second
# instance: man's default search path is the absolute /usr/man, and under b6sim the system
# calls are the HOST's, so that path is the BUILD MACHINE's -- on this machine's builder it
# is whatever the host installed, and a case that read it would assert something about
# whoever ran the build.  The bare form, with no -M and no $MANPATH, is asserted under the
# booted kernel in kernel/test/filters and nowhere else.
#
# THE -M PATHS ARE RELATIVE, which is forced rather than tidy: the working directory is this
# binary directory, and `man -w' PRINTS THE PATH IT WAS GIVEN, so an absolute -M would put a
# build directory straight into a checked-in .expected and the tree would not build anywhere
# else.  ../../more/test/CMakeLists.txt met the same thing through a banner.
#
# THE FIXTURE PAGES ARE NOT NAMED *.umm, and this is a hard constraint and not a preference.
# ../../man2umm/test/ globs cmd/, lib/ and include/ RECURSIVELY for its man_lint_* tests and
# registers one test per page named after the file, so a fixture ls.1.umm here would collide
# with the real ../../ls/ls.1.umm and CMake would fail to configure.  Naming them the way the
# IMAGE names them -- tree/man1/ls.1, no suffix -- avoids that and is more faithful besides:
# these are stand-ins for staged pages, not for sources.  Their contents are one line each,
# because what is under test is which file man picks, never what is in it.
#
# THREE THINGS THESE CASES CANNOT SAY:
#
#   * THE DEFAULT /usr/man, above.
#   * $MANPATH AND $PAGER.  ../../../scripts/run-prog-test.sh runs `env -i', so no variable
#     reaches the guest; $MANPATH is asserted in kernel/test/filters and $PAGER in neither
#     world, for the next reason.
#   * EVERYTHING AFTER system(3), which is the whole display path -- the case where man is
#     neither -w nor -.  b6sim forks, but the child's exec of /bin/sh would load the BUILD
#     MACHINE's shell, which is not a BESM-6 a.out.  There is deliberately NO case asserting
#     empty output for it: an empty .expected passes for a dozen wrong reasons.  The booted
#     kernel runs it against /bin/cat and diffs the result against the page file itself.
#
# What is left is the whole of man's decision-making -- which file, in which order, out of
# which path element -- and that is where a manual browser is actually wrong when it is
# wrong.  `commands' is the case that matters most: thirteen page names live in two sections
# at once on the real image, and it pins that the table's order and not a directory's decides.
#
set(MAN_FIXTURES
    tree/man1/ls.1
    tree/man1/fsck.1m
    tree/man1/mount.1m
    tree/man1/passwd.1
    tree/man2/mount.2
    tree/man3/stdio.3s
    tree/man3/exp.3m
    tree/man5/passwd.5
    tree/man8/init.8
    other/man1/ls.1)

set(_copied)
foreach(f ${MAN_FIXTURES})
    get_filename_component(_dir ${CMAKE_CURRENT_BINARY_DIR}/${f} DIRECTORY)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_dir}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/man/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(man_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests man_fixtures)
endif()

# DEST because this program is /usr/bin/man and not bin/man -- see ../CMakeLists.txt.
b6_progtest(man where    DEST usr/bin/man) # -w, a relative -M, and a first-probe hit
b6_progtest(man subsect  DEST usr/bin/man) # an exact subsection suffix, letter and all
b6_progtest(man family   DEST usr/bin/man) # `3' matches the whole 3* family: stdio.3s
b6_progtest(man family3m DEST usr/bin/man) # ... and exp.3m, so the rule is not 3s-specific
b6_progtest(man sect1m   DEST usr/bin/man) # `1' reaches 1m: section 1M lives in man1
b6_progtest(man bare     DEST usr/bin/man) # no section at all: the whole table is walked
b6_progtest(man commands DEST usr/bin/man) # COMMANDS FIRST: mount.1m, not mount.2
b6_progtest(man all      DEST usr/bin/man) # -a gives both, in table order; -aw clusters
b6_progtest(man explicit DEST usr/bin/man) # an explicit section overrides that order
b6_progtest(man path     DEST usr/bin/man) # the colon list: path outer, section inner
b6_progtest(man reset    DEST usr/bin/man) # a section governs ONE title and not the next
b6_progtest(man cat      DEST usr/bin/man) # `-': the page source, no formatter, no pager
b6_progtest(man catall   DEST usr/bin/man) # ... and two of them, concatenated in order
b6_progtest(man noent    DEST usr/bin/man) # a miss under -w: diagnosed, and STATUS 1
b6_progtest(man nosect   DEST usr/bin/man) # a section digit no table entry answers
b6_progtest(man nopath   DEST usr/bin/man) # a -M element that is not a directory: a miss
b6_progtest(man partial  DEST usr/bin/man) # a miss does not throw away the hit before it
b6_progtest(man usage    DEST usr/bin/man) # no arguments at all
