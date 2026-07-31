# quot's b6sim cases -- task C4a.
#
# quot reads a FILESYSTEM, and a flat b6fsutil image is an ordinary host file, so these run
# against real on-disk structure rather than only exercising argument handling.
# ../../df/test/CMakeLists.txt is where that fixture is built and where the argument for it
# is made; this directory copies the finished image rather than building a second one, so
# there is exactly one description of what is in it (../../df/test/fsimg.manifest).
#
# THE PER-UID REPORT CANNOT BE ASSERTED HERE, and finding out why is worth writing down.
# quot maps a uid to a name through getpwent(3), which opens the literal path /etc/passwd --
# so under b6sim it reads THE BUILD MACHINE's password file, and every name it prints is a
# property of whoever is building.  There is no uid this can be steered around:
#
#   * quot ignores uids >= NUID (300) outright, so every uid it CAN report is inside the
#     range both macOS and Linux fill with system accounts.  On the machine this was written
#     on, the fixture's uid 202 came back as `_coreaudiod'.
#
#   * uid 0 is unavoidable -- the root directory has an owner -- and is not `root' either:
#     macOS opens /etc/passwd with a `##' comment header, and getpwent(3) has no notion of a
#     comment (v7's password files had none), so the first line parses as an entry named
#     `##' with an empty uid field, which is 0.  It wins, du[0].name being set once.
#
# So the two cases that print names are gone and only `-c' remains, which looks at no name
# at all.  kernel/test/fsinfo is where the per-uid report is asserted, against a host oracle
# that RECOMPUTES the sums out of `b6fsutil -v -v' -- on an image whose /etc/passwd is ours.
#
# WHAT THESE CASES STILL CANNOT SAY, and it is the same disclaimer df's carry: none of the
# four alignment rules quot.c's header lists exists under b6sim, whose read(2) is the host's.
# An unaligned buffer, a partial-block count and an off-block seek all succeed here and all
# fail -- three with EFAULT, the fourth in silence -- on the real kernel.  kernel/test/fsinfo
# is where the device path is asserted, and against a host oracle that RECOMPUTES the
# per-uid sums out of `b6fsutil -v -v', so it needs no updating as the image grows.
#
# NO `-n' CASE, and task C4e did not change that.  ../../ncheck is on the image now, so the
# listing -n reads has a producer at last -- but b6_progtest() runs ONE program and pipes
# nothing, and a hand-made <case>.in would assert this program against a fixture invented
# for it rather than against ncheck's real output.  The place for the pipeline is a SIMH
# test, where a shell can run one; ../../TODO.md's C4e note is where that would go.

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/cmd/df/test/fsimg.img ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img
    DEPENDS df_fsimg
    COMMENT "copy cmd/df/test/fsimg.img" VERBATIM)
add_custom_target(quot_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img)

# ctest does not build, so the fixture hangs on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests quot_fsimg)
endif()

# DEST, because quot is /etc/quot and not /bin/quot -- see ../CMakeLists.txt.
#
# `sizes' is the whole of the arithmetic, and it exercises BOTH halves of it: the fixture has
# a file of EXACTLY one filesystem block and one of exactly three, so a ceil-divide by 3072
# that was off by one either way would move a count -- and the first and third columns are
# then multiplied into the 1024-byte block quot REPORTS in, while the second, a count of
# FILES, is not.  So the expected `3  6  18' is six one-block objects: 6 files, 6 blocks, 18
# reported.  Six one-block objects and one three-block file is the shape to check against
# ../../df/test/fsimg.manifest by hand if this ever changes.
b6_progtest(quot sizes DEST etc/quot) # -c: the histogram, which looks at no name at all
b6_progtest(quot nodev DEST etc/quot) # the diagnostic, and that v7's quot still exits 0
