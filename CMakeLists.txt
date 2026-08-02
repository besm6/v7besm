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
# Installed from here rather than from lib/, which owns only the compiled libc.a/libm.a/crt0.o.
# lib/ builds against this tree in the source directory (-I../include), so it needs no install
# ordering: `make' builds the cmd/ tools, the kernel and lib/ together, and `make install'
# installs the headers here and the archives from lib/.
#
install(DIRECTORY include/ DESTINATION share/besm6/include
        FILES_MATCHING PATTERN "*.h")

#
# The BESM-6 cross-builds: the kernel, the user libraries (lib/), and the native user
# programs staged into build/rootfs/ (cmd/init, and whatever joins it -- b6_prog() in
# scripts/BesmCross.cmake).  Unlike the cmd/ tools above, none is
# compiled by this project's host compiler -- all cross-compile with the b6* toolchain built
# above (they use those in-tree targets directly, so a rebuilt tool relinks them with no
# reinstall).  All still depend on the EXTERNAL c-compiler being installed, though -- b6cc's
# b6parse/b6lower/b6codegen passes, its <besm6.h>, and libruntime.a all come from ~/.local (or
# /usr/local) and are not built here.  Guard on that runtime so a fresh tree without the
# external compiler still configures and builds the cmd/ tools; libruntime.a co-ships with the
# passes and headers, so its presence is a good single signal.
#
# scripts/BesmCross.cmake resolves the shared cross toolchain (and the b6_obj/b6_find_src
# helpers) once for both subdirs; include it here, inside the guard, so kernel/ and lib/ inherit
# it rather than each resolving the tools itself.
#
find_file(B6RUNTIME_LIB libruntime.a
    PATHS $ENV{HOME}/.local/share/besm6/lib /usr/local/share/besm6/lib)
if(B6RUNTIME_LIB)
    message(STATUS "Found external runtime ${B6RUNTIME_LIB} -- building kernel/, lib/ and the rootfs")
    include(scripts/BesmCross.cmake)

    #
    # The staged root filesystem, build/rootfs/ -- the tree that becomes the disk image the
    # kernel mounts (root.manifest, at the top of the tree).  `rootfs' is the aggregate every
    # b6_prog() hangs itself on; the programs come after lib/, since each links against its libc.
    #
    # DECLARED BEFORE add_subdirectory(lib), not after it as it used to be, because lib/test
    # now stages its programs onto the image too (as /usr/test/*, task 25c) and hangs them on
    # this target through the same `if(TARGET rootfs)' guard b6_prog() uses.  An aggregate with
    # no dependencies of its own can be created anywhere; only the guards care.
    #
    # ALL, so that a plain `make' stages the whole tree.  It matters since lib/test joined:
    # those programs are staged by their EXISTING image targets (lib_<name>_img, a POST_BUILD
    # copy), and those live in the test aggregate rather than in ALL -- so without this,
    # `make' alone would leave build/rootfs/usr/test empty and only `make test' would fill it.
    add_custom_target(rootfs ALL)

    add_subdirectory(kernel)
    add_subdirectory(lib)

    add_subdirectory(cmd/init)
    add_subdirectory(cmd/sh)
    add_subdirectory(cmd/basename)
    add_subdirectory(cmd/cal)
    add_subdirectory(cmd/cat)
    add_subdirectory(cmd/chgrp)
    add_subdirectory(cmd/chmod)
    add_subdirectory(cmd/chown)
    add_subdirectory(cmd/clri)
    add_subdirectory(cmd/cmp)
    add_subdirectory(cmd/col)
    add_subdirectory(cmd/comm)
    add_subdirectory(cmd/cp)
    add_subdirectory(cmd/date)
    add_subdirectory(cmd/dcheck)
    add_subdirectory(cmd/dd)
    add_subdirectory(cmd/df)
    add_subdirectory(cmd/diff)
    add_subdirectory(cmd/diffh)
    add_subdirectory(cmd/du)
    add_subdirectory(cmd/echo)
    add_subdirectory(cmd/ed)
    add_subdirectory(cmd/fgrep)
    add_subdirectory(cmd/file)
    add_subdirectory(cmd/find)
    add_subdirectory(cmd/fsck)
    add_subdirectory(cmd/getty)
    add_subdirectory(cmd/grep)
    add_subdirectory(cmd/icheck)
    add_subdirectory(cmd/join)
    add_subdirectory(cmd/kill)
    add_subdirectory(cmd/ln)
    add_subdirectory(cmd/login)
    add_subdirectory(cmd/look)
    add_subdirectory(cmd/ls)
    add_subdirectory(cmd/mesg)
    add_subdirectory(cmd/mkdir)
    add_subdirectory(cmd/mkfs)
    add_subdirectory(cmd/mount)
    add_subdirectory(cmd/mv)
    add_subdirectory(cmd/newgrp)
    add_subdirectory(cmd/ncheck)
    add_subdirectory(cmd/novi)
    add_subdirectory(cmd/od)
    add_subdirectory(cmd/passwd)
    add_subdirectory(cmd/pr)
    add_subdirectory(cmd/pwd)
    add_subdirectory(cmd/quot)
    add_subdirectory(cmd/rev)
    add_subdirectory(cmd/rm)
    add_subdirectory(cmd/rmdir)
    add_subdirectory(cmd/sed)
    add_subdirectory(cmd/sleep)
    add_subdirectory(cmd/sort)
    add_subdirectory(cmd/split)
    add_subdirectory(cmd/stty)
    add_subdirectory(cmd/su)
    add_subdirectory(cmd/sum)
    add_subdirectory(cmd/sync)
    add_subdirectory(cmd/tail)
    add_subdirectory(cmd/tar)
    add_subdirectory(cmd/tee)
    add_subdirectory(cmd/test)
    add_subdirectory(cmd/time)
    add_subdirectory(cmd/touch)
    add_subdirectory(cmd/tr)
    add_subdirectory(cmd/tsort)
    add_subdirectory(cmd/tty)
    add_subdirectory(cmd/uniq)
    add_subdirectory(cmd/umount)
    add_subdirectory(cmd/wall)
    add_subdirectory(cmd/wc)
    add_subdirectory(cmd/who)
    add_subdirectory(cmd/write)
    add_subdirectory(cmd/yes)
    add_subdirectory(etc)

    #
    # The disk image is built out of build/rootfs/, and kernel/test/ was configured above --
    # before any of it existed -- so this edge cannot be drawn there: add_dependencies()
    # needs both targets to be defined, and `rootfs' is not, that early.  It is drawn here,
    # where both are.  Without it a parallel build can run b6fsutil over a rootfs that is
    # only half staged.
    #
    if(TARGET rootimg)
        add_dependencies(rootimg rootfs)
    endif()
else()
    message(STATUS "kernel/, lib/ and the rootfs: external compiler runtime (libruntime.a) not installed; skipping")
endif()
