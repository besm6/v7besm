# df's b6sim cases -- task C4a, and the harness the rest of C4 inherits.
#
# EVERY EARLIER cmd/ TEST DIRECTORY ASSERTS ARGUMENT HANDLING AND LITTLE ELSE, because
# b6sim services each system call on the host and there is nothing of this machine left to
# look at.  df is the first program here for which that is not the whole story: what it
# reads is a FILESYSTEM, and a flat b6fsutil image is an ordinary host file.  So the fixture
# below is a real v7 filesystem, and `cmd_df_image' reads its real superblock and reports on
# real blocks -- in a tenth of a second, with no boot.  mkfs(1) (C4c) and fsck(1) (C4d) want
# exactly this and should copy it rather than invent a second one.
#
# NO CASE HERE MAY REACH df's loadtab(), and that is a rule and not an observation.  Under
# b6sim every system call is the HOST's, so the moment df opens /etc/mtab it reads the BUILD
# MACHINE's table -- absent on macOS, and on Linux a symlink to /proc/self/mounts, which would
# also put mtab.c's "more than 8 entries" warning on stderr.  df.c's loadtab() is lazy for
# exactly this reason, and the argument shapes that never reach it are the three used below:
# a name stat(2) cannot find, a REGULAR FILE that validates as a filesystem image, and a bad
# flag.  A case naming a device, a mount point or an ordinary non-image file would read the
# host's table and must not be added here.  ../../mount/test/CMakeLists.txt is the account.
#
# `cmd_df_walk' IS THE ONE THAT EARNS ITS KEEP.  walk.expected is image.expected CHARACTER FOR
# CHARACTER -- `cmp' them -- because `df' reads s_tfree and `df -w' walks the free list, and
# the two readings of one number must agree.  b6fsutil -v says 186 free, which is the third.
#
# WHAT IT STILL CANNOT SAY.  None of the four alignment rules df.c's header lists exists
# here: b6sim's read(2) is the host's, so an unaligned buffer, a partial-block count and an
# off-block seek all succeed under this harness and all fail with EFAULT -- or, for the
# seek, silently -- on the real kernel.  A green `cmd_df_image' therefore says the
# arithmetic is right and says NOTHING about the device path.  kernel/test/fsinfo is where
# that is asserted, and ../README.md is the account.
#
# NOTHING BINARY IS CHECKED IN.  The fixture's files are runs of one 64-byte line, written
# at configure time, and the image is built from them at build time; fsimg.manifest says
# why each size is the size it is.

# The image must be byte-reproducible or its free-block count is not an expectation, so the
# timestamp is fixed exactly as ROOTTIME fixes root.img's (kernel/test/CMakeLists.txt).  It
# is a different number on purpose: nothing correlates the two, and reusing ROOTTIME would
# suggest something does.
set(FSIMG_TIME 1700000000)
set(FSIMG_BLOCKS 200)

# One 64-byte line, repeated.  64 divides 3072, so a size in lines is a size in bytes and
# `mid' lands on a block boundary exactly rather than nearly.
set(FSIMG_LINE "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n")

# string(REPEAT) is CMake 3.15 and this project requires 3.10, so the loop is the portable
# spelling.  The files are small; this costs nothing at configure time.
function(fsimg_file name nlines)
    set(_c "")
    foreach(_i RANGE 1 ${nlines})
        string(APPEND _c "${FSIMG_LINE}")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fsimg.d/${name} "${_c}")
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fsimg.d)
fsimg_file(small 1)     #    64 bytes -- 1 block, reported as 3
fsimg_file(mid 48)      #  3072 bytes -- exactly 1 block, reported as 3: the off-by-one
fsimg_file(big 144)     #  9216 bytes -- 3 blocks, all direct, reported as 9
fsimg_file(other 24)    #  1536 bytes -- half a block, so 1, reported as 3
fsimg_file(nested 2)    #   128 bytes -- 1 block under a directory, reported as 3

# The manifest's `source' paths resolve against the WORKING_DIRECTORY below, which is why
# fsimg.d/ is generated into the build tree and named relatively -- the same arrangement
# ../../../root.manifest has with kernel/test's build directory.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img
    COMMAND $<TARGET_FILE:b6fsutil> -n -s ${FSIMG_BLOCKS} -T ${FSIMG_TIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/fsimg.manifest fsimg.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fsimg.manifest b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil cmd/df/test/fsimg.img" VERBATIM)
add_custom_target(df_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img)

# ctest does not build, so the fixture has to hang on the target `make test' does build --
# the same reason b6_progtest() hangs the program itself there.
if(TARGET build_tests)
    add_dependencies(build_tests df_fsimg)
endif()

b6_progtest(df image)   # the table off a real superblock -- in the 1024-byte block df REPORTS
                        # in, so 558 where the image has 186 free filesystem blocks and 585
                        # where it has 195 data blocks.  b6fsutil -v says both.
b6_progtest(df walk)    # -w: the same numbers by walking the free list.  walk.expected is
                        # image.expected character for character, and that is the assertion
b6_progtest(df inode)   # -i: (s_isize - (SUPERB+1)) * INOPB == 128, which is what RetroBSD's
                        # `- 2' would have made 96 -- there is no boot block on this machine
b6_progtest(df nodev)   # the diagnostic, and that v7's df still exits 0 after it
b6_progtest(df badflag) # ...whereas a bad FLAG exits 1, and prints a usage line naming no
                        # program: argv[0] under b6sim is the staged path in the build tree
