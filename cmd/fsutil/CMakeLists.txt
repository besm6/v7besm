#
# Unix v7 filesystem image builder for the BESM-6.
#
# A host tool that creates, inspects, checks and populates filesystem images for
# the kernel in kernel/.  The flat container it writes is a plain file of blocks,
# each 512 words, each word six big-endian bytes -- the same encoding b6as and
# b6ld use, and it comes from the same place: fputw()/fgetw() in cmd/libaout.
# Keeping one implementation is the point; the two must not be able to disagree
# about what a word looks like on disk.
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(fsengine STATIC
    params.cpp
    image.cpp
    superblock.cpp
    simh.cpp
    alloc.cpp
    inode.cpp
    dir.cpp
    create.cpp
    manifest.cpp
    command.cpp
    check.cpp
)

#
# NOTE the absence of ${CMAKE_SOURCE_DIR}/include here.  params.cpp does read the
# kernel's sys/param.h, but it reaches it by relative path instead, because
# include/ is the kernel's FULL system-header tree -- its own ctype.h, stdio.h and
# string.h shadow the host's, and nothing in this tool compiles with it on the
# include path.  See the comment at that include for the details.
#
target_include_directories(fsengine PUBLIC
    ${BESM6_CROSS_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fsengine PUBLIC aout)

add_executable(b6fsutil main.cpp)
target_link_libraries(b6fsutil PRIVATE fsengine)
install(TARGETS b6fsutil RUNTIME DESTINATION bin)

add_subdirectory(test)
