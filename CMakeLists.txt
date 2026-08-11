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
add_subdirectory(cmd/man2umm)
add_subdirectory(cmd/yacc)
add_subdirectory(cmd/lex)

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
    # ALL, so that a plain `make' stages the whole tree.
    add_custom_target(rootfs ALL)

    #
    # ...and build/testfs/, the staging tree of the SECOND filesystem: the lib/test programs,
    # which used to be /usr/test on the root image and are now /test on a pack of their own
    # (test.manifest, beside root.manifest).  `testfs' is to lib/test what `rootfs' is to
    # cmd/: the aggregate every stager hangs itself on.
    #
    # DECLARED BEFORE add_subdirectory(lib), because that is where the only stager is and it
    # hangs on this target through the same `if(TARGET ...)' guard b6_prog() uses.  An
    # aggregate with no dependencies of its own can be created anywhere; only the guards care.
    #
    # ALL for the reason rootfs is: those programs are staged by their EXISTING image targets
    # (lib_<name>_img, a POST_BUILD copy), and those live in the test aggregate rather than in
    # ALL -- so without this, `make' alone would leave build/testfs/test empty and only
    # `make test' would fill it.
    add_custom_target(testfs ALL)

    #
    # THE DEVELOPMENT TREE ON THE IMAGE: /lib and /usr/include, which task C9e put
    # there because /usr/bin/cc's link line names them.  Without these the machine's
    # own driver can only link -nostdlib, which is a driver that cannot build a
    # program.
    #
    # Three lists, declared HERE rather than beside the copies below because
    # kernel/test/ is configured before them and its ROOTFS_FILES has to name the
    # same files -- root.img depends on every one of them, and a file staged but not
    # listed is an image that silently does not rebuild.
    #
    # B6_STAGE_LIB is what `cc' names by default and nothing more: crt0.o and libc.a
    # from lib/libc, and libruntime.a from the external c-compiler beside it.
    # libm.a, libcurses.a and libtermcap.a are deliberately NOT here -- some 40
    # blocks for archives nothing on this image links -- and adding one is a line
    # here and a stanza in root.manifest.
    #
    # The header tree has TWO OWNERS, as install(DIRECTORY include/) below records:
    # the hosted half is this repo's and is staged from the SOURCE tree, so an edit
    # is on the image without an install; the freestanding half is the external
    # compiler's, which defines those ten and which we do not ship copies of.
    #
    set(B6_STAGE_INC_SRC ${CMAKE_SOURCE_DIR}/include)
    file(GLOB B6_STAGE_INC RELATIVE ${B6_STAGE_INC_SRC}
         ${B6_STAGE_INC_SRC}/*.h ${B6_STAGE_INC_SRC}/sys/*.h)
    get_filename_component(B6_STAGE_FREEINC_SRC ${B6LIBDIR}/../include ABSOLUTE)
    set(B6_STAGE_FREEINC
        besm6.h float.h iso646.h limits.h stdalign.h
        stdarg.h stdbool.h stddef.h stdint.h stdnoreturn.h)
    set(B6_STAGE_LIB crt0.o libc.a libruntime.a)

    #
    # THE MANUAL, /usr/man -- two hundred pages in the dialect doc/Manual_Page_Format.md
    # describes, which is the whole of it: every page in the tree goes on the image.  There
    # is a man(1) to find them with (cmd/man, task C25b) and a manview(1) to format them
    # (cmd/manview, task C25a), and the pages are staged AS SOURCES either way: a page is
    # legible as it stands, which is what the dialect is for, so `man - ls' shows the file
    # itself and nothing on this disk is preformatted.
    #
    # v7's OWN LAYOUT: the section DIGIT picks the directory and the subsection letter stays
    # on the FILE, so fsck's page is man1/fsck.1m and stdio's is man3/stdio.3s.  The .umm
    # suffix is a source-tree convention and is dropped here.  A page is COPIED from where it
    # lives beside its source (cmd/README.md §10), so an edit reaches the disk with no
    # install, exactly as a header does.
    #
    # ONE LIST, and it holds IMAGE-RELATIVE names -- `man1/ls.1' -- for the reason the three
    # above are declared here rather than beside the copies: kernel/test/ is configured at the
    # next line and root.img has to name every one of them.  The source path is consumed in
    # the loop below and never leaves it, so there is no second list to skew against.
    #
    # SIX GLOBS AND NOT A RECURSIVE ONE.  cmd/man2umm/test/ globs cmd/, lib/ and include/
    # recursively for the man_lint_* tests and today finds these same two hundred files -- but
    # a .umm FIXTURE under some cmd/<x>/test/ would belong in that corpus and must not thereby
    # land on the disk.  The two sets coincide and are allowed to diverge in that direction.
    # Like every file(GLOB) here it runs at CONFIGURE time: a new page needs a re-configure,
    # exactly as a new header does, and a stanza in root.manifest besides.
    #
    set(B6_STAGE_MAN "")
    set(_staged_man "")
    file(GLOB _man_pages
         ${CMAKE_SOURCE_DIR}/cmd/*/*.umm
         ${CMAKE_SOURCE_DIR}/lib/libc/man/*.umm
         ${CMAKE_SOURCE_DIR}/lib/libm/*.umm
         ${CMAKE_SOURCE_DIR}/lib/libtermcap/*.umm
         ${CMAKE_SOURCE_DIR}/lib/libcurses/*.umm
         ${CMAKE_SOURCE_DIR}/include/man/*.umm)
    foreach(_p ${_man_pages})
        get_filename_component(_page ${_p} NAME)              # ls.1.umm
        string(REGEX REPLACE "\\.umm$" "" _page ${_page})     # ls.1
        if(NOT _page MATCHES "^.+\\.([0-9])[a-z]*$")
            message(FATAL_ERROR "manual page ${_p}: name is not <topic>.<section>.umm")
        endif()
        set(_dst man${CMAKE_MATCH_1}/${_page})                # man1/ls.1, man1/fsck.1m
        add_custom_command(OUTPUT ${B6_ROOTFS}/usr/man/${_dst}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/man/man${CMAKE_MATCH_1}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_p} ${B6_ROOTFS}/usr/man/${_dst}
            DEPENDS ${_p}
            COMMENT "stage /usr/man/${_dst}" VERBATIM)
        list(APPEND B6_STAGE_MAN ${_dst})
        list(APPEND _staged_man ${B6_ROOTFS}/usr/man/${_dst})
    endforeach()
    add_custom_target(rootfs_man ALL DEPENDS ${_staged_man})
    add_dependencies(rootfs rootfs_man)

    add_subdirectory(kernel)
    add_subdirectory(lib)

    #
    # ...and the copies themselves, which have to come after lib/ since two of the
    # three archives are its output.  copy_if_different rather than copy: a header
    # that did not change must not restamp the file the disk image depends on, or
    # every configure would rebuild root.img.
    #
    set(_staged "")
    foreach(_f ${B6_STAGE_LIB})
        if(_f STREQUAL libruntime.a)
            set(_src ${B6LIBDIR}/${_f})     # the external compiler's
            set(_dep ${_src})
        else()
            set(_src ${B6_LIBC_DIR}/${_f})  # lib/libc's, freshly built
            set(_dep libc)
        endif()
        add_custom_command(OUTPUT ${B6_ROOTFS}/lib/${_f}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/lib
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_src} ${B6_ROOTFS}/lib/${_f}
            DEPENDS ${_dep}
            COMMENT "stage /lib/${_f}" VERBATIM)
        list(APPEND _staged ${B6_ROOTFS}/lib/${_f})
    endforeach()

    foreach(_f ${B6_STAGE_INC})
        get_filename_component(_d ${B6_ROOTFS}/usr/include/${_f} DIRECTORY)
        add_custom_command(OUTPUT ${B6_ROOTFS}/usr/include/${_f}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_d}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${B6_STAGE_INC_SRC}/${_f} ${B6_ROOTFS}/usr/include/${_f}
            DEPENDS ${B6_STAGE_INC_SRC}/${_f}
            COMMENT "stage /usr/include/${_f}" VERBATIM)
        list(APPEND _staged ${B6_ROOTFS}/usr/include/${_f})
    endforeach()

    foreach(_f ${B6_STAGE_FREEINC})
        add_custom_command(OUTPUT ${B6_ROOTFS}/usr/include/${_f}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/include
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${B6_STAGE_FREEINC_SRC}/${_f} ${B6_ROOTFS}/usr/include/${_f}
            DEPENDS ${B6_STAGE_FREEINC_SRC}/${_f}
            COMMENT "stage /usr/include/${_f}" VERBATIM)
        list(APPEND _staged ${B6_ROOTFS}/usr/include/${_f})
    endforeach()

    add_custom_target(rootfs_devel ALL DEPENDS ${_staged})
    add_dependencies(rootfs rootfs_devel)

    add_subdirectory(cmd/init)
    # /usr/bin/cpp -- the host tool cmd/cpp, built a second time for the target (task C9a).
    # A subdirectory of its own because cmd/cpp is added above, outside this guard, where
    # b6_prog() does not exist yet; cmd/cpp/rootfs/CMakeLists.txt says so at length.
    add_subdirectory(cmd/cpp/rootfs)
    # aoutt -- not a program on the image but a TEST of cmd/libaout, run under b6sim
    # from build/rootfs/test/.  It has to live here for the same reason the rest do:
    # cmd/libaout is added above, outside this guard.  It comes first because what it
    # covers -- the word path in fgetw()/fputw() -- is underneath every one of them.
    add_subdirectory(cmd/libaout/rootfs)
    # /usr/bin/as and /usr/bin/ld -- the same story one task later (C9b).  Each is
    # a subdirectory for the same reason cmd/cpp/rootfs is, and each says so.
    add_subdirectory(cmd/as/rootfs)
    add_subdirectory(cmd/ld/rootfs)
    # /usr/bin/{size,disasm,nm,strip} -- the read-only binutils, task C9c, and what
    # makes the three above usable: the machine can now read back what it built.
    add_subdirectory(cmd/size/rootfs)
    add_subdirectory(cmd/disasm/rootfs)
    add_subdirectory(cmd/nm/rootfs)
    add_subdirectory(cmd/strip/rootfs)
    # ...and lorder, which came along with nm: a shell script, so it is staged
    # rather than compiled, but it is the same one-source-two-copies shape.
    add_subdirectory(cmd/lorder/rootfs)
    # /usr/bin/ar and /usr/bin/ranlib -- task C9d, and the archive the objects
    # above go into: with these the machine builds its own libc.a, index and all.
    # ranlib carries the WHOLE of ar inside it (ranlib_run() calls ar_run()), so
    # there are two copies of the archiver on the disk; cmd/ranlib/rootfs says why
    # that was the choice.  They are also the first native programs to link the
    # full B6_LIBAOUT_SOURCES -- the four file-descriptor routines exist for them.
    add_subdirectory(cmd/ar/rootfs)
    add_subdirectory(cmd/ranlib/rootfs)
    # /usr/bin/cc -- task C9e, and the last of C9: the driver that runs all of the
    # above.  One source file, no libaout (a driver reads no a.out), and the only
    # program of the ten that cannot do its most obvious job -- the compiler proper
    # is the external c-compiler's, so `cc foo.c' on the machine stops after the
    # preprocessor.  It is also what makes /lib and /usr/include below worth having.
    add_subdirectory(cmd/cc/rootfs)
    # /usr/bin/yacc and /usr/lib/yaccpar -- task C10c, the toolchain's twelfth
    # program -- and calct, which is NOT on the image: the one test that says
    # b6parse accepts the parser skeleton cmd/yacc emits (task C10a).  Here
    # rather than in cmd/yacc/ because that is added above, outside this guard,
    # where b6_prog() and b6_yacc() do not exist -- as cmd/libaout/rootfs is.
    add_subdirectory(cmd/yacc/rootfs)
    # /usr/bin/lex and /usr/lib/lex/ncform -- task C10d, the thirteenth -- and
    # scant, likewise not on the image: the one test that says b6parse accepts
    # the scanner skeleton cmd/lex emits (task C10b), and b6_lex()'s first
    # caller.  Here for the reason cmd/yacc/rootfs is.
    add_subdirectory(cmd/lex/rootfs)
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
    add_subdirectory(cmd/dmesg)
    add_subdirectory(cmd/du)
    add_subdirectory(cmd/echo)
    add_subdirectory(cmd/ed)
    add_subdirectory(cmd/egrep)
    add_subdirectory(cmd/expr)
    add_subdirectory(cmd/fgrep)
    add_subdirectory(cmd/file)
    add_subdirectory(cmd/find)
    add_subdirectory(cmd/fsck)
    add_subdirectory(cmd/getty)
    add_subdirectory(cmd/grep)
    add_subdirectory(cmd/icheck)
    add_subdirectory(cmd/iostat)
    add_subdirectory(cmd/join)
    add_subdirectory(cmd/kill)
    add_subdirectory(cmd/ln)
    add_subdirectory(cmd/login)
    add_subdirectory(cmd/look)
    add_subdirectory(cmd/ls)
    add_subdirectory(cmd/m4)
    add_subdirectory(cmd/make)
    add_subdirectory(cmd/man)
    add_subdirectory(cmd/manview)
    add_subdirectory(cmd/mesg)
    add_subdirectory(cmd/mkdir)
    add_subdirectory(cmd/mkfs)
    add_subdirectory(cmd/more)
    add_subdirectory(cmd/mount)
    add_subdirectory(cmd/mv)
    add_subdirectory(cmd/newgrp)
    add_subdirectory(cmd/ncheck)
    add_subdirectory(cmd/nice)
    add_subdirectory(cmd/novi)
    add_subdirectory(cmd/od)
    add_subdirectory(cmd/passwd)
    add_subdirectory(cmd/pr)
    add_subdirectory(cmd/ps)
    add_subdirectory(cmd/pstat)
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
    add_subdirectory(cmd/vmstat)
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
    if(TARGET testimg)
        add_dependencies(testimg testfs)
    endif()
else()
    message(STATUS "kernel/, lib/ and the rootfs: external compiler runtime (libruntime.a) not installed; skipping")
endif()
