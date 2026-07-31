# dd's b6sim cases -- task C4b.
#
# WHAT THIS HARNESS CAN AND CANNOT SAY ABOUT dd.  It can say everything about the command
# language and the record bookkeeping, which is most of the program: b6sim runs the staged
# /bin/dd and services every system call on the host, so a copy through stdin and stdout, the
# arithmetic of bs=/count=/skip=, the conversion tables and each diagnostic's exit status are
# all reachable here in a tenth of a second and with no boot.
#
# It can say NOTHING about the four alignment rules dd.c's header lists.  b6sim's read(2) is
# the host's, so an unaligned buffer, a partial-block count and an off-block seek all succeed
# under this harness and all fail -- with EFAULT, or for the seek silently -- on the real
# kernel.  ../../../kernel/test/dd is where the raw path is asserted, and ../../df/README.md
# is the account of the rules themselves.
#
# TWO CASES EXIST TO PIN A DIVERGENCE, and they are the ones to look at first if either ever
# needs changing.  `word' proves that the `w' suffix is one 48-bit word, six bytes, where v7's
# was two: twelve bytes of input come out as 2+0 records, and would be 6+0 on a PDP-11.
# `block' proves that `b' is a filesystem block, 3072 bytes, where v7's was 512: the 200-block
# fixture image comes out as 200+0 records, and would be 1200+0 with v7's multiplier.  Both
# would pass silently against the wrong constant if the assertion were only "it copied".
#
# THE EBCDIC PAYLOAD IS NOT ASSERTED HERE, deliberately.  The round trip that would prove the
# two tables are inverses needs dd run TWICE -- ascii to ebcdic and back -- and one case is
# one program, so it lives in kernel/test/dd, where a shell can chain them and the host can
# cmp the result out of the extracted image.  What is asserted here instead is the cbs=
# bookkeeping around them, which is text: `cbs' truncates one record and pads another, and
# the counters say so on stderr while the bytes go to a file nobody reads.

# The fixture is ../../df/test's, copied rather than rebuilt -- ../../df/README.md asks the
# rest of C4 to reuse it rather than invent a second one, and ../../quot/test does the same.
# It is exactly 200 blocks of 3072 bytes, which is what makes `block' an assertion.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/cmd/df/test/fsimg.img ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img
    DEPENDS df_fsimg
    COMMENT "copy cmd/df/test/fsimg.img" VERBATIM)
add_custom_target(dd_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img)

# ctest does not build, so the fixture has to hang on the target `make test' does build --
# the same reason b6_progtest() hangs the program itself there.
if(TARGET build_tests)
    add_dependencies(build_tests dd_fsimg)
endif()

b6_progtest(dd copy)   # stdin to stdout with no arguments at all -- there is no copy.args --
                       # so the default record is a whole block and twelve bytes are one
                       # PARTIAL record each way
b6_progtest(dd bs)     # bs= sets both, and the fast path (conv unset) copies without the
                       # per-byte loop
b6_progtest(dd count)  # count= stops after n input records
b6_progtest(dd skip)   # skip= discards n input records by reading them, as v7 did, because
                       # the input may be a pipe
b6_progtest(dd swab)   # THE B2 REGRESSION.  Six bytes are three pairs; v7's `(ibc>>1) & ~1'
                       # swapped two of them and left the third, giving badcef where this
                       # gives badcfe.  At the default block size the two agree, which is
                       # exactly why the case names an odd number of pairs
b6_progtest(dd ucase)  # conv=ucase, and with it the cnull() path that conv= reaches when no
                       # table is named
b6_progtest(dd word)   # `w' is NBPW == 6 (see above)
b6_progtest(dd block)  # `b' is BSIZE == 3072 (see above)
b6_progtest(dd cbs)    # conv=ebcdic with cbs=: one record truncated, one padded with EBCDIC
                       # blanks, and the counters reporting it
b6_progtest(dd badarg) # THE B1 ASSERTIONS are the three .status files: v7 printed each of
b6_progtest(dd zero)   # these diagnostics and then exited 0, so no script could tell a
b6_progtest(dd nofile) # refused copy from a completed one
