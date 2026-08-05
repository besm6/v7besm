#
# manview's b6sim cases -- task C25a.
#
# THE FIXTURE PAGES ARE NOT NAMED *.umm, and this is a hard constraint and not a preference,
# for ../../man/test/CMakeLists.txt's reason: ../../man2umm/test/ globs cmd/, lib/ and
# include/ RECURSIVELY for its man_lint_* tests and registers one ctest per page named after
# the file, so a fixture spans.1.umm here would join the real corpus -- be linted as though
# it were a manual page, and collide by name with anything else called that.  They are named
# the way the IMAGE names a page instead: page/man1/x.1, no suffix.
#
# THEY ARE @srcdir@ ARGUMENTS AND NOT COPIED IN, which ../../more/test could not do and this
# can: manview prints a file name in exactly one place, perror() on an open that failed, so
# the `noent' case below is the only one whose output could carry a build path and it names
# a RELATIVE path for that reason.  Everything else prints the page and never its name.
#
# WHAT THESE CASES CAN SAY IS ALL OF IT, which is unusual here and is worth stating: manview
# is a filter with no system call but open, read and write, no environment, no terminal and
# no fork.  There is nothing in it that only the booted kernel can reach.  The one thing
# asserted elsewhere is the WIRING -- that man(1) actually runs this program -- and that is
# section 21 of kernel/test/filters.sh, where system(3) really forks a shell.
#
# THE ESCAPE SEQUENCES ARE IN THE .expected FILES AS BYTES.  A case that ran -p everywhere
# would test the parser twice and the back end never; `spans' and `plain' are deliberately
# the same page through the two, so the diff between them is exactly the attribute layer.
#
b6_progtest(manview spans    DEST usr/bin/manview) # the four span kinds, rule 2, rule 3
b6_progtest(manview plain    DEST usr/bin/manview) # ... the same page under -p
b6_progtest(manview mono     DEST usr/bin/manview) # ... and under -m: attributes, no colour
b6_progtest(manview xref     DEST usr/bin/manview) # name(N), and the three things that are not
b6_progtest(manview blocks   DEST usr/bin/manview) # every block kind of section 4
b6_progtest(manview deflist  DEST usr/bin/manview) # the hanging tag, short and long, and nested
b6_progtest(manview banner   DEST usr/bin/manview) # the level-1 title, with and without [extra]
b6_progtest(manview width    DEST usr/bin/manview) # -w, and a word wider than the measure
b6_progtest(manview utf8     DEST usr/bin/manview) # a column is a CHARACTER (../../README.md §11)
b6_progtest(manview stdin    DEST usr/bin/manview) # no operand at all
b6_progtest(manview two      DEST usr/bin/manview) # two pages in one command, as man -a gives
b6_progtest(manview noent    DEST usr/bin/manview) # perror on a relative path, and STATUS 1
b6_progtest(manview usage    DEST usr/bin/manview) # an option that is not one
