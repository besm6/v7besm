#
# BESM-6 user-level simulator: runs a.out executables and services Unix v7
# system calls (extracode 077).
#
add_library(simulator STATIC
    session.cpp
    memory.cpp
    machine.cpp
    processor.cpp
    arithmetic.cpp
    besm6_arch.cpp
    syscall.cpp
    tty.cpp
    kernel.cpp
    etcfiles.cpp
    params.cpp
    trace.cpp
)
#
# NOTE the absence of ${CMAKE_SOURCE_DIR}/include here, as in cmd/fsutil.  params.cpp
# does read the kernel's sys/param.h -- it exists to static_assert kernel.cpp's copy of
# the tunables against it -- but it reaches it by relative path instead, because
# include/ is the kernel's FULL system-header tree: its own stdio.h, string.h and
# sys/stat.h shadow the host's, and syscall.cpp needs the host's.  See the comment at
# that include.  test/etc_test.cpp reaches the tree the same way and for the same reason,
# but with a -D rather than a relative path: see test/CMakeLists.txt.
#
target_include_directories(simulator PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simulator PUBLIC aout)

add_executable(b6sim main.cpp)
target_link_libraries(b6sim PRIVATE simulator)
install(TARGETS b6sim RUNTIME DESTINATION bin)

# Embed the git commit hash and revision count into the version string.
execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_HASH
)
execute_process(
    COMMAND git rev-list HEAD --count
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_REVCOUNT
)
set_source_files_properties(session.cpp PROPERTIES COMPILE_FLAGS
    -DVERSION_STRING=\\"0.1.${GIT_REVCOUNT}-${GIT_HASH}\\"
)

add_subdirectory(test)
