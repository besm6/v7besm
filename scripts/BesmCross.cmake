# Shared BESM-6 cross-toolchain resolution for the CMake builds that DON'T use the host
# compiler -- the kernel, the user libraries, and the native user programs under cmd/.
# All three emit BESM-6 a.out through the b6* tools rather than through CMake's C-language
# support, and all need the same things: the tools themselves, the external compiler's
# libruntime.a, and the compile/find/link helpers.
# This file is that common half, so it lives in one place and is included by
#   - the top-level CMakeLists.txt (once, inside the libruntime guard), whence its variables
#     and functions are inherited by add_subdirectory(kernel)/add_subdirectory(lib); and
#   - a standalone `cmake -S kernel' or `cmake -S lib', which has no parent to inherit from
#     and so includes it directly (guarded by `if(NOT COMMAND b6_obj)').
# include_guard is belt-and-suspenders against a second include reaching here.
include_guard(GLOBAL)

# This directory, captured at include time: b6_prog() runs check-size.sh from here, and
# CMAKE_CURRENT_LIST_DIR inside a function would name the CALLER's list file instead.
set(B6_SCRIPTS_DIR ${CMAKE_CURRENT_LIST_DIR})

# The cross toolchain.  Two modes:
#  - Integrated in the top-level project, the tools are CMake targets already being built in
#    build/, so use THOSE -- editing cmd/as, cmd/ld, ... and rebuilding then relinks with the
#    fresh tool, no `make install' in between.  b6cc is a driver, though: it shells out to
#    b6cpp/b6as/b6ld (ours) and b6parse/b6lower/b6codegen (the EXTERNAL c-compiler, not built
#    here).  B6CC_ENV points the three we build at the build tree via b6cc's per-tool env
#    overrides; the external passes have no override and resolve from the install, as they must.
#  - Standalone (`cmake -S kernel'/`-S lib'), no such targets exist, so fall back to the
#    installed tools found on PATH's standard prefixes.
if(TARGET b6cc)
    set(B6CC     $<TARGET_FILE:b6cc>)
    set(B6AS     $<TARGET_FILE:b6as>)
    set(B6LD     $<TARGET_FILE:b6ld>)
    set(B6AR     $<TARGET_FILE:b6ar>)
    set(B6RANLIB $<TARGET_FILE:b6ranlib>)
    set(B6NM     $<TARGET_FILE:b6nm>)
    set(B6SIZE   $<TARGET_FILE:b6size>)
    set(B6STRIP  $<TARGET_FILE:b6strip>)
    set(B6DISASM $<TARGET_FILE:b6disasm>)
    set(B6SIM    $<TARGET_FILE:b6sim>)
    set(B6YACC   $<TARGET_FILE:b6yacc>)
    set(B6CC_ENV ${CMAKE_COMMAND} -E env
        B6CPP=$<TARGET_FILE:b6cpp> B6AS=$<TARGET_FILE:b6as> B6LD=$<TARGET_FILE:b6ld>)
    # b6yacc reads its parser skeleton from a data file, and a build tree has not
    # installed one -- same shape as B6CC_ENV above, and the same reason: the
    # tool's own search path is the INSTALL's, and nothing here may depend on it.
    # The path is kept as well, so b6_yacc() can depend on the file: editing the
    # skeleton changes every generated parser and no target-level dependency sees it.
    set(B6YACCPAR ${CMAKE_SOURCE_DIR}/cmd/yacc/yaccpar.c)
    set(B6YACC_ENV ${CMAKE_COMMAND} -E env B6YACCPAR=${B6YACCPAR})
else()
    find_program(B6CC     b6cc     REQUIRED)
    find_program(B6AS     b6as     REQUIRED)
    find_program(B6LD     b6ld     REQUIRED)
    find_program(B6AR     b6ar     REQUIRED)
    find_program(B6RANLIB b6ranlib REQUIRED)
    find_program(B6NM     b6nm     REQUIRED)
    find_program(B6SIZE   b6size   REQUIRED)
    find_program(B6STRIP  b6strip  REQUIRED)
    find_program(B6DISASM b6disasm REQUIRED)
    find_program(B6SIM    b6sim    REQUIRED)
    find_program(B6YACC   b6yacc   REQUIRED)
    set(B6CC_ENV "")
    # the installed b6yacc finds the installed skeleton by itself, which is what
    # its search path is for
    set(B6YACCPAR "")
    set(B6YACC_ENV "")
endif()

# besm6 (the SIMH full-machine simulator) boots the kernel; b6sim (above) runs a user a.out.
# Only the kernel needs besm6, and only to boot/test, so it stays optional.
find_program(BESM6 besm6)

# libruntime.a (the b$* helpers) and besm6.h come from the EXTERNAL compiler's install, never
# the build tree -- so locate the runtime directly rather than deriving it from b6cc (whose
# build-tree path has no share/besm6/lib beside it).  Required: neither the kernel nor libc can
# link without it.  This is also the top-level guard's signal that the external compiler is
# installed.
find_file(B6LIBRUNTIME libruntime.a
    PATHS $ENV{HOME}/.local/share/besm6/lib /usr/local/share/besm6/lib REQUIRED)
get_filename_component(B6LIBDIR ${B6LIBRUNTIME} DIRECTORY)

# Where b6_prog() stages the native user programs, and where their libc comes from.  The
# staging tree is a root filesystem in the making -- build/rootfs/etc/init is what ends up
# as /etc/init on the disk image (root.manifest, at the top of the tree) -- so nothing but files bound
# for the image may be written into it; the .nm/.dis listings stay in the build dir.
# Both are overridable, so a standalone `cmake -S lib' with its own binary tree, or a
# caller staging elsewhere, is not forced into this layout.
if(NOT DEFINED B6_ROOTFS)
    set(B6_ROOTFS ${CMAKE_BINARY_DIR}/rootfs)
endif()

# And the staging tree of the SECOND filesystem, the test pack: build/testfs/test/hello is
# /test/hello on test.img (test.manifest, beside root.manifest) and /mnt/test/hello once it is
# mounted.  A tree of its own rather than a corner of B6_ROOTFS, so that B6_ROOTFS goes on
# meaning exactly what the root image holds.  Only lib/test/ stages into it; b6_prog() does
# not, and its DEST is still relative to B6_ROOTFS.
if(NOT DEFINED B6_TESTFS)
    set(B6_TESTFS ${CMAKE_BINARY_DIR}/testfs)
endif()
if(NOT DEFINED B6_LIBC_DIR)
    set(B6_LIBC_DIR ${CMAKE_BINARY_DIR}/lib/libc)
endif()

# The three optional archives b6_prog()'s LIBS keyword can name, beside the libc every program
# links.  Same derivation as B6_LIBC_DIR and overridable for the same reason.
if(NOT DEFINED B6_LIBM_DIR)
    set(B6_LIBM_DIR ${CMAKE_BINARY_DIR}/lib/libm)
endif()
if(NOT DEFINED B6_LIBCURSES_DIR)
    set(B6_LIBCURSES_DIR ${CMAKE_BINARY_DIR}/lib/libcurses)
endif()
if(NOT DEFINED B6_LIBTERMCAP_DIR)
    set(B6_LIBTERMCAP_DIR ${CMAKE_BINARY_DIR}/lib/libtermcap)
endif()

# ---------------------------------------------------------------------------------------
# Compile one source to an object.  Dispatches by extension:
#   .s        -> b6as, no flags (assembly that #includes nothing)
#   .c / .S   -> b6cc -c -I<inc> [ARGN], where .S is preprocessed (cpp -> as) because it
#               #includes headers.  ARGN carries e.g. -DKERNEL for the kernel sources.
# The object lands in ${B6_OBJDIR} if the caller set it, else the current binary dir -- a
# test/ subdir sets it per-program so images do not share an object output (which the
# Makefiles generator would emit into every consuming target and race under `make -j').
# Reads ${KINC}/${KHDRS} from the CALLER's scope (CMake dynamic scoping), so each subdir keeps
# setting its own header path and coarse dependency.  Sets ${outvar} to the object path.
# ---------------------------------------------------------------------------------------
function(b6_obj outvar abssrc)
    get_filename_component(base ${abssrc} NAME_WE)
    get_filename_component(ext ${abssrc} EXT)
    if(DEFINED B6_OBJDIR)
        set(obj ${B6_OBJDIR}/${base}.o)
    else()
        set(obj ${CMAKE_CURRENT_BINARY_DIR}/${base}.o)
    endif()
    if(ext STREQUAL ".s")
        add_custom_command(OUTPUT ${obj}
            COMMAND ${B6AS} ${abssrc} -o ${obj}
            DEPENDS ${abssrc}
            COMMENT "b6as ${base}.o" VERBATIM)
    else()
        add_custom_command(OUTPUT ${obj}
            COMMAND ${B6CC_ENV} ${B6CC} -c ${ARGN} -I${KINC} ${abssrc} -o ${obj}
            DEPENDS ${abssrc} ${KHDRS}
            COMMENT "b6cc ${base}.o" VERBATIM)
    endif()
    set(${outvar} ${obj} PARENT_SCOPE)
endfunction()

# Find a source by basename, searching the caller's dir, then .., then ../dev -- the CMake
# equivalent of a Makefile's `vpath %.c .. ../dev'.  Local wins, so a test's own crt0.s is
# preferred over any next door.  Used by the kernel test dir.
#
# file(GLOB) rather than if(EXISTS): on a case-insensitive filesystem EXISTS(crt0t.s) is true
# because crt0t.S is present, which would route a .S source (needs cpp) to b6as.  GLOB returns
# the entry's REAL case, and STREQUAL below is case-sensitive, so .S and .s are told apart.
function(b6_find_src outvar base)
    foreach(dir ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../dev)
        file(GLOB _cands "${dir}/${base}.*")
        foreach(cand ${_cands})
            get_filename_component(_ext ${cand} EXT)
            if(_ext STREQUAL ".c" OR _ext STREQUAL ".s" OR _ext STREQUAL ".S")
                get_filename_component(_abs ${cand} ABSOLUTE)
                set(${outvar} ${_abs} PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
    message(FATAL_ERROR "b6_find_src: no source for '${base}'")
endfunction()

# ---------------------------------------------------------------------------------------
# Run b6yacc over a grammar and hand back a C source b6_obj() can compile.
#
#   b6_yacc(<outvar> <name> <abs .y>)
#
# Always -vd: y.tab.h costs nothing and a grammar that wants its token numbers elsewhere
# needs it, and y.output is the only readable account of a conflict.
#
# Sets ${outvar} to the generated source and ${outvar}_DIR to the directory holding it,
# so a caller that #includes y.tab.h passes CFLAGS -I${<outvar>_DIR} to b6_prog().
#
# A WORKING DIRECTORY OF ITS OWN, per grammar.  yacc writes y.tab.c, y.tab.h and
# y.output under those exact names in the current directory -- they are the interface and
# cannot be renamed on the command line -- so two grammars generating into one directory
# clobber each other's output under `make -j' for the same reason kernel/test/'s per-program
# object dirs exist.  (The two scratch files that used to join them are tmpfile() streams
# now; cmd/yacc/dextern.h says so.)  Created eagerly at configure time, as b6_prog() creates
# B6_OBJDIR.
#
# The generated source is RENAMED to <name>.c, because b6_obj() derives the object's name
# from the source's and CMake's NAME_WE cuts `y.tab.c' at the first dot -- every grammar in
# the tree would compile to the same y.o.  y.tab.h keeps its name: that is what a grammar's
# own #include says.
#
# $<TARGET_FILE:b6yacc> in the COMMAND is what makes a rebuilt b6yacc regenerate every
# parser with no `make install' -- CMake reads a target-level dependency out of it.
#
# THIS SERVES THE CROSS BUILDS ONLY, being inside the libruntime guard.  cmd/lex (task C10b)
# is a HOST program built from a b6yacc-generated parser and cannot use this; it names
# $<TARGET_FILE:b6yacc> in a custom command of its own.
# ---------------------------------------------------------------------------------------
function(b6_yacc outvar name absy)
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/${name}.yacc.dir)
    file(MAKE_DIRECTORY ${_dir})
    set(_src ${_dir}/${name}.c)

    add_custom_command(OUTPUT ${_src} ${_dir}/y.tab.h ${_dir}/y.output
        COMMAND ${B6YACC_ENV} ${B6YACC} -vd ${absy}
        COMMAND ${CMAKE_COMMAND} -E rename y.tab.c ${_src}
        DEPENDS ${absy} ${B6YACCPAR} ${B6YACC}
        WORKING_DIRECTORY ${_dir}
        COMMENT "b6yacc ${name}.c" VERBATIM)

    set(${outvar} ${_src} PARENT_SCOPE)
    set(${outvar}_DIR ${_dir} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------------------
# Build one NATIVE BESM-6 user program and stage it into the root filesystem tree.
#
#   b6_prog(<name> DEST <path under ${B6_ROOTFS}> SOURCES <src>...
#           [CFLAGS <flag>...] [LIBS libm|libcurses|libtermcap ...])
#
# This is the third kind of thing this repo builds, beside the host tools of cmd/ and the
# freestanding artifacts of kernel/: a program compiled by the b6* toolchain, linked
# against the libc built in lib/, and destined for the disk image the kernel mounts.
# cmd/init was the first; cmd/cpp/rootfs (task C9a) is the one that made the ceilings bite.
#
# Defines a target b6prog_<name> producing ${B6_ROOTFS}/<DEST>, makes the aggregate
# `rootfs' target depend on it if that target exists, and registers one ctest,
# rootfs_<name>_size (label `rootfs'), asserting the program fits the user address space.
#
# THE LINK ORDER IS A CONTRACT, not a style: crt0.o, then the objects, then whatever LIBS
# names, then -lc -lruntime.  b6ld scans each archive exactly once, in order; libm, libcurses
# and libtermcap call into libc, libc calls the b$* helpers, and nothing calls back
# (lib/README.md).  Hence the order LIBS is emitted in is this function's, not the caller's:
# -lm, then -lcurses, then -ltermcap, because curses is what calls tgetent/tgetstr/tgoto/tputs
# and termcap calls nothing back.  A program that names libcurses must name libtermcap too.
# lib/test/CMakeLists.txt's b6_libtest() does the same thing for the libc test programs.
#
# The caller must have set KINC/KHDRS, which b6_obj reads from its scope.
# ---------------------------------------------------------------------------------------
function(b6_prog name)
    cmake_parse_arguments(P "PURE" "DEST" "SOURCES;CFLAGS;LIBS" ${ARGN})
    if(NOT P_DEST OR NOT P_SOURCES)
        message(FATAL_ERROR "b6_prog(${name}): DEST and SOURCES are both required")
    endif()

    # PURE -> b6ld -n -> NMAGIC: const+text become a SHARED TEXT SEGMENT and data is pushed
    # to the next page boundary.  On this machine "pure" means shared, not protected -- РЗ
    # closes a page to reads as well as writes, so a read-only text page would take the
    # program's own constant pool with it, and estabur() ignores the distinction.  What it
    # buys is one copy of the text in core however many processes are running the binary,
    # which is the only thing that makes kernel/text.c's xalloc/xexpand/xccdec reachable at
    # all: getxfile() forces ux_tsize to 0 for an FMAGIC image and xalloc() returns at once.
    if(P_PURE)
        set(_pure -n)
    else()
        set(_pure "")
    endif()

    # One object directory per program, for the reason b6_obj's comment gives: a shared
    # object rule is emitted into every consuming target by the Makefiles generator and
    # races under `make -j'.  Also lets two programs share a source basename.
    set(B6_OBJDIR ${CMAKE_CURRENT_BINARY_DIR}/${name}.dir)
    file(MAKE_DIRECTORY ${B6_OBJDIR})
    set(objs "")
    foreach(src ${P_SOURCES})
        get_filename_component(abs ${src} ABSOLUTE)
        b6_obj(obj ${abs} ${P_CFLAGS})
        list(APPEND objs ${obj})
    endforeach()

    # LIBS, in the contract's order and not the caller's.  An unknown name is an error rather
    # than a silently dropped -l: b6ld would only report the undefined symbols it caused.
    foreach(_l ${P_LIBS})
        if(NOT _l MATCHES "^lib(m|curses|termcap)$")
            message(FATAL_ERROR "b6_prog(${name}): LIBS knows libm, libcurses and libtermcap, not '${_l}'")
        endif()
    endforeach()
    set(_libs "")
    set(_libdeps "")
    set(_libtargets "")
    set(B6_LIBM_A        ${B6_LIBM_DIR}/libm.a)
    set(B6_LIBCURSES_A   ${B6_LIBCURSES_DIR}/libcurses.a)
    set(B6_LIBTERMCAP_A  ${B6_LIBTERMCAP_DIR}/libtermcap.a)
    foreach(_l m curses termcap)
        if("lib${_l}" IN_LIST P_LIBS)
            string(TOUPPER ${_l} _u)
            list(APPEND _libs -L${B6_LIB${_u}_DIR} -l${_l})
            list(APPEND _libdeps ${B6_LIB${_u}_A})
            list(APPEND _libtargets lib${_l})
        endif()
    endforeach()

    set(out ${B6_ROOTFS}/${P_DEST})
    get_filename_component(outdir ${out} DIRECTORY)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${outdir}
        COMMAND ${B6LD} ${_pure} ${B6_LIBC_DIR}/crt0.o ${objs} ${_libs} -o ${out}
                -L${B6_LIBC_DIR} -L${B6LIBDIR} -lc -lruntime
        COMMAND sh -c "${B6NM} -n ${out} > ${name}.nm"
        COMMAND sh -c "${B6DISASM} -c ${out} > ${name}.dis"
        DEPENDS ${objs} ${B6_LIBC_DIR}/crt0.o ${B6_LIBC_DIR}/libc.a ${_libdeps}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "b6ld ${P_DEST}" VERBATIM)

    add_custom_target(b6prog_${name} ALL DEPENDS ${out})
    # libc lives in another directory scope, so the file dependency above is not enough to
    # order the two -- as lib/test/CMakeLists.txt does for the same reason.  Same for LIBS.
    add_dependencies(b6prog_${name} libc ${_libtargets})
    if(TARGET rootfs)
        add_dependencies(rootfs b6prog_${name})
    endif()

    # 28 pages of image (32 less the four the stack takes at 070000) and a 15-bit pointer
    # reach: doc/Memory_Mapping.md, and kernel/TODO.md task 24.
    add_test(NAME rootfs_${name}_size
        COMMAND sh ${B6_SCRIPTS_DIR}/check-size.sh
                ${B6SIZE} ${B6NM} ${out} 28672 32767)
    set_tests_properties(rootfs_${name}_size PROPERTIES LABELS rootfs)
endfunction()

# ---------------------------------------------------------------------------------------
# Register one b6sim case for a program built by b6_prog().
#
#   b6_progtest(<prog> <case> [DEST <path under ${B6_ROOTFS}>])
#
# Runs the STAGED program -- ${B6_ROOTFS}/bin/<prog> by default, the very bytes that go on
# the disk image -- under b6sim with the arguments in <case>.args, and diffs its combined
# output against <case>.expected, checking <case>.status on the way.  ../run-prog-test.sh is
# the harness and its header states what may and may not be asserted there; lib/test's
# b6_libtest()/run-test.sh is the model, and cmd/README.md §9 the reason there are two
# worlds at all.
#
# DEST names the staged path when it is not bin/<prog>, and must be the same string the
# program's own b6_prog() call was given.  Task C4a's quot(1) is the first to need it -- it
# is /etc/quot, being a section-1M program that reads the raw device -- and the rest of C4
# (mkfs, fsck, icheck, dcheck, ncheck, clri, mount, umount) is /etc too.
#
# The ctest name is cmd_<prog>_<case> and the label is `cmd', so `ctest -L cmd' is the
# userland regression corpus without a two-minute boot.  Files live in cmd/<prog>/test/.
# ---------------------------------------------------------------------------------------
# One b6sim case: files <case>.args (arguments, @srcdir@ substituted), <case>.in (standard
# input, /dev/null when absent -- task C3, for ed, whose whole command language arrives there),
# <case>.expected (stdout and stderr together) and <case>.status (the exit status, 0 when
# absent), all in cmd/<prog>/test/.  See scripts/run-prog-test.sh, which is the harness, and
# cmd/README.md SS9 for which of the two worlds a case belongs in.
function(b6_progtest prog case)
    cmake_parse_arguments(T "" "DEST" "" ${ARGN})
    if(NOT T_DEST)
        set(T_DEST bin/${prog})
    endif()
    add_test(NAME cmd_${prog}_${case}
        COMMAND sh ${B6_SCRIPTS_DIR}/run-prog-test.sh
                ${B6SIM} ${CMAKE_CURRENT_SOURCE_DIR} ${B6_ROOTFS}/${T_DEST} ${case}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_${prog}_${case} PROPERTIES LABELS cmd)
    # `make test' builds build_tests and nothing else, so the program the case runs has to
    # be hung on it -- ctest does not build.
    if(TARGET build_tests AND TARGET b6prog_${prog})
        add_dependencies(build_tests b6prog_${prog})
    endif()
endfunction()
