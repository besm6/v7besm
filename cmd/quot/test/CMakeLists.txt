# quot's b6sim cases -- task C4a.
#
# quot reads a FILESYSTEM, and a flat b6fsutil image is an ordinary host file, so these run
# against real on-disk structure rather than only exercising argument handling.
# ../../df/test/CMakeLists.txt is where that fixture is built and where the argument for it
# is made; this directory copies the finished image rather than building a second one, so
# there is exactly one description of what is in it (../../df/test/fsimg.manifest).
#
# THE PER-UID REPORT IS ASSERTED HERE, and for a while it could not be.  quot maps a uid to
# a name through getpwent(3), which opens the literal path /etc/passwd -- and b6sim used to
# hand that path to the host, so every name printed was a property of whoever was building.
# There was no uid it could be steered around: quot ignores uids >= NUID (300), so the range
# it CAN report is the one both macOS and Linux fill with system accounts (the fixture's 202
# came back `_coreaudiod' on the machine this was written on), and uid 0 is unavoidable, the
# root directory having an owner.  b6sim serves the TARGET's /etc/passwd now
# (cmd/sim/etcfiles.cpp), so the names are the image's: 0 is `root', and 201 and 202 are in
# that file no more than they were in anyone's, so quot's `#%d' fallback prints them.  The
# two cases below say exactly what kernel/test/fsinfo's oracle says of the same arithmetic.
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
# test, where a shell can run one.  ../../../kernel/test/mount.sh is where it would go now:
# task C4f's section 6 already runs ncheck over /dev/rmd1 with a shell around it.

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
#
# The report, and the two arms of it.  ../../df/test/fsimg.manifest is what to check these
# against by hand: root owns the root directory, /d and /small -- 3 one-block objects, 9
# reported -- 201 owns /mid and /big, one block and three, and 202 owns /other and /d/nested.
# The rows come out descending by blocks, and `-f' adds the FILE COUNT, the one column the
# `sizes' case never reaches.
#
b6_progtest(quot report DEST etc/quot) # blocks per uid, and the name that goes with it
b6_progtest(quot files DEST etc/quot)  # -f: and how many files made that count
