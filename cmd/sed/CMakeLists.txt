# sed(1) -- task C5e, the seventeenth of the text filters.
#
# THREE SOURCES, WHICH IS THE FIRST THING ABOUT THIS PORT.  v7's sed.h DEFINED every one of
# the program's forty-odd globals -- `char genbuf[LBSIZE];' -- in a header both sed0.c and
# sed1.c include, and let the PDP-11 linker merge the two common blocks.  C11 has no
# tentative definition across translation units and b6ld has no common symbols, so without
# sedglob.c each half would get its own copy: sed0.c would compile a script into its
# ptrspace and sed1.c would execute an empty one, with no diagnostic from anything.
# cmd/sh/defs.h and cmd/sh/glob.c are the precedent and the only other instance in cmd/.
#
# THE TASK'S OWN SUBJECT is the two 128-entry tables, both indexed by an unmasked byte:
# the character-class bitmap, which is 256 bits here as grep's became in task C5c, and the
# y/// translation table, which nothing had flagged and which was a wild READ where grep's
# was a wild store.  And the replacement text of an `s' command, which marked a
# backslash-escaped byte by setting bit 0200 of it -- ed(1)'s QESC prefix replaces that.
# See README.md beside this file.
#
# sed.1.umm is v7's page corrected in place, as ../README.md SS10 requires.  manview(1)
# formats it on the image and it is legible as it stands either way.  The Cyrillic example
# in it is UTF-8 like everything else and needs no flag.
#
# NOT SETUID: sed opens files for reading, and creates the wfiles it was told to, as
# whoever ran it.  ../README.md SS8.
#
# NOT PURE: nothing runs several seds at once, and a page-aligned data segment costs a
# page.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# AND sed's OWN header, which that glob does not reach: b6_obj's coarse dependency is the
# SYSTEM header tree, so a program whose constants live in a header of its own gets no
# dependency on it at all and editing sed.h rebuilds nothing.  It cost an afternoon here --
# every measurement of the recursion ceiling was taken against a binary that still had the
# old MAXDEPTH in it -- and cmd/sh has the same shape and the same line.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/sed.h)

b6_prog(sed DEST bin/sed
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sed0.c
                ${CMAKE_CURRENT_SOURCE_DIR}/sed1.c
                ${CMAKE_CURRENT_SOURCE_DIR}/sedglob.c)

add_subdirectory(test)
