cmake_minimum_required(VERSION 3.10)
project(v7besm C CXX)

# The new Apple/LLD linkers resolve symbols from all archives in one pass, so
# repeating static libraries (as a traditional linker needs) makes them warn:
#   ld: warning: ignoring duplicate libraries: '.../libfoo.a'
# Enable CMake's linker-capability-based de-duplication to drop the repeats.
# (Both policies are OLD by default because cmake_minimum_required is 3.10.)
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall -Werror -Wshadow")
set(CMAKE_C_FLAGS   "-Wall -Werror -Wshadow")

#
# Enable cppcheck by default
#
find_program(Cppcheck NAMES cppcheck)
if(NOT (Cppcheck MATCHES "NOTFOUND"))
    message(STATUS "Found Cppcheck: ${Cppcheck}")
    set(CMAKE_CXX_CPPCHECK "${Cppcheck}"
        "--std=c++17"
        "--enable=style"
        "--check-level=exhaustive"
        "--error-exitcode=1" # Fail on any issues
        "--inline-suppr" # Enable inline control like // cppcheck-suppress "id"
        "--quiet" # No progress report messages
        "--suppress=badBitmaskCheck" # Allow redundant zero operands
        "--suppress=uninitMemberVarNoCtor" # Allow member variables without initializer
        "--suppress=*:*/_deps/*" # Ignore issues in Googletest
        "--library=${CMAKE_SOURCE_DIR}/scripts/googletest.xml" # Parse TEST() macro properly
    )
    set(CMAKE_C_CPPCHECK "${Cppcheck}"
        "--std=c11"
        "--enable=style"
        "--check-level=exhaustive"
        "--error-exitcode=1" # Fail on any issues
        "--inline-suppr" # Enable inline control like // cppcheck-suppress "id"
        "--quiet" # No progress report messages
    )
endif()

# Download GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    EXCLUDE_FROM_ALL
)
set(BUILD_GMOCK OFF CACHE BOOL "Builds the googlemock subproject" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Enable installation of googletest" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)
enable_testing()

#
# BESM-6 cross headers (besm6/b.out.h, besm6/ar.h, ...).  The toolchain sources
# include them as "besm6/b.out.h", so this directory must be on the include path
# of every BESM-6 host tool.
#
set(BESM6_CROSS_INCLUDE ${CMAKE_SOURCE_DIR}/cross)

#
# Aggregate target that builds every unit-test executable without building the
# whole project (used by `make test`). Each test subdirectory registers its
# executable via add_dependencies(build_tests <exe>).
#
add_custom_target(build_tests)

add_subdirectory(cmd/libaout)
add_subdirectory(cmd/cpp)
add_subdirectory(cmd/as)
add_subdirectory(cmd/disasm)
add_subdirectory(cmd/sim)
add_subdirectory(cmd/ld)
add_subdirectory(cmd/ar)
add_subdirectory(cmd/nm)
add_subdirectory(cmd/ranlib)
add_subdirectory(cmd/size)
add_subdirectory(cmd/strip)
add_subdirectory(cmd/lorder)
add_subdirectory(cmd/cc)
add_subdirectory(cmd/fsutil)

#
# The system header tree.  b6cc appends <prefix>/share/besm6/include to every
# preprocessor run, so this is where a <stdio.h> or a <string.h> compiled with
# no -I of its own is found.  There is exactly one such tree, and this repo owns
# the hosted half of it: the v7 headers, sys/ included, plus the few C11 ones
# libc.a backs.  The freestanding half -- <stddef.h>, <stdarg.h>, <besm6.h> and
# the rest of those ten -- is installed into the same directory by the external
# c-compiler, which is what defines them; we do not ship copies.
#
# Installed from here rather than from lib/, because these are needed to compile
# lib/libc itself -- the bootstrap is `make install', then `make -C lib', then
# `make -C lib install'.  See lib/libc/Makefile for the other two steps.
#
install(DIRECTORY include/ DESTINATION share/besm6/include
        FILES_MATCHING PATTERN "*.h")
