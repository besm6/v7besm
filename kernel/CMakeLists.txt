# Unix v7 kernel for the BESM-6.
#
# Cross-compiled by the *installed* b6* toolchain (b6cc/b6as/b6ld/b6ar/b6ranlib/
# b6nm/b6size/b6disasm), which emit BESM-6 a.out -- not host objects -- so this is a
# `project(... NONE)' build that drives the tools through custom commands rather than
# CMake's C-language support.  It is standalone and out-of-source: `make install' must
# have installed the toolchain before it can run.  Its counterpart is `lib/', likewise
# outside the top-level CMake project for the same reason.
#
# The image must end below 064000 (KEND); the link ends by printing `b6size -w unix'.
# Works two ways: standalone (`cd kernel && make', its own project) and as a subdirectory
# of the top-level CMake project (add_subdirectory(kernel)).  When included, the parent owns
# the project(), so declare our own only when we are the top of the tree -- and note that
# CMAKE_SOURCE_DIR then points at the repo root, not here, so paths below use
# CMAKE_CURRENT_SOURCE_DIR instead.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.10)
    project(kernel NONE)
endif()

# The cross toolchain and the SIMH simulator.  besm6 is only needed to boot/test, so it
# is optional -- the image builds without it.
find_program(B6CC     b6cc     REQUIRED)
find_program(B6AS     b6as     REQUIRED)
find_program(B6LD     b6ld     REQUIRED)
find_program(B6AR     b6ar     REQUIRED)
find_program(B6RANLIB b6ranlib REQUIRED)
find_program(B6NM     b6nm     REQUIRED)
find_program(B6SIZE   b6size   REQUIRED)
find_program(B6DISASM b6disasm REQUIRED)
find_program(BESM6    besm6)

# libruntime.a (the b$* helpers) sits beside the compiler's install prefix, so derive it
# from b6cc rather than hard-coding ~/.local -- this follows ~/.local vs /usr/local.
get_filename_component(_bin ${B6CC} DIRECTORY)
get_filename_component(B6LIBDIR ${_bin}/../share/besm6/lib ABSOLUTE)

# The one system-header tree, and a coarse dependency on all of sys/: b6cc has no -M, so
# every object simply depends on every system header (the same trade the test dir makes).
# Absolute, from CMAKE_CURRENT_SOURCE_DIR so it is right whether standalone or integrated.
get_filename_component(KINC ${CMAKE_CURRENT_SOURCE_DIR}/../include ABSOLUTE)
file(GLOB KHDRS ${KINC}/sys/*.h)

# ---------------------------------------------------------------------------------------
# Compile one source to an object in the current binary dir.  Dispatches by extension:
#   .s        -> b6as, no flags (brz.s/switch.s #include nothing)
#   .c / .S   -> b6cc -c -I<inc> [ARGN], where .S is preprocessed (cpp -> as) because it
#               #includes headers.  ARGN carries -DKERNEL for the kernel sources.
# Sets ${outvar} to the absolute object path.  Defined here so the test/ subdir reuses it.
# ---------------------------------------------------------------------------------------
function(b6_obj outvar abssrc)
    get_filename_component(base ${abssrc} NAME_WE)
    get_filename_component(ext ${abssrc} EXT)
    set(obj ${CMAKE_CURRENT_BINARY_DIR}/${base}.o)
    if(ext STREQUAL ".s")
        add_custom_command(OUTPUT ${obj}
            COMMAND ${B6AS} ${abssrc} -o ${obj}
            DEPENDS ${abssrc}
            COMMENT "b6as ${base}.o" VERBATIM)
    else()
        add_custom_command(OUTPUT ${obj}
            COMMAND ${B6CC} -c ${ARGN} -I${KINC} ${abssrc} -o ${obj}
            DEPENDS ${abssrc} ${KHDRS}
            COMMENT "b6cc ${base}.o" VERBATIM)
    endif()
    set(${outvar} ${obj} PARENT_SCOPE)
endfunction()

# Find a source by basename, searching local dir, then .., then ../dev -- the CMake
# equivalent of the test Makefile's `vpath %.c .. ../dev'.  Local wins, so a test's own
# crt0.s is preferred over any next door.  Used only by test/.
#
# file(GLOB) rather than if(EXISTS): on this case-insensitive filesystem EXISTS(crt0t.s)
# is true because crt0t.S is present, which would route a .S source (needs cpp) to b6as.
# GLOB returns the entry's REAL case, and STREQUAL below is case-sensitive, so .S and .s
# are told apart -- the same hazard the old Makefile's `%.o: %.S' comment describes.
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
# The kernel image.
# ---------------------------------------------------------------------------------------

# Core kernel (SYS) + drivers (DEV) + machine assist (MACH).  Every one gets -DKERNEL;
# b6_obj ignores it for the .s members (brz.s, switch.s), matching the Makefile.
set(KSRC
    acct.c alloc.c clock.c conf.c fio.c iget.c intr.c machdep.c main.c malloc.c
    nami.c pipe.c prf.c prim.c rdwri.c sendsig.c sig.c slp.c subr.c sys1.c sys2.c
    sys3.c sys4.c syscall.c sysent.c text.c trap.c utab.c
    dev/bio.c dev/mb.c dev/md.c dev/mem.c dev/partab.c dev/sc.c dev/sr.c dev/sys.c dev/tty.c
    brz.s uarea.S seg.S usermem.S switch.s)

set(KOBJS "")        # absolute object paths, for DEPENDS
set(KOBJ_NAMES "")   # bare basenames, for the `b6ar' command line
foreach(src ${KSRC})
    b6_obj(o ${CMAKE_CURRENT_SOURCE_DIR}/${src} -DKERNEL)
    list(APPEND KOBJS ${o})
    get_filename_component(n ${o} NAME)
    list(APPEND KOBJ_NAMES ${n})
endforeach()

# besm6.o is linked FIRST and is NOT archived: its const contribution pins the 0500/0501
# interrupt vectors at their fixed addresses.
b6_obj(BESM6_O ${CMAKE_CURRENT_SOURCE_DIR}/besm6.S -DKERNEL)

# libunix.a -- one link-pulled archive so unused kernel code is dropped; b6ranlib's symbol
# index lets the drivers' back-references into the core resolve in a single scan.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libunix.a
    COMMAND ${B6AR} cr libunix.a ${KOBJ_NAMES}
    COMMAND ${B6RANLIB} libunix.a
    DEPENDS ${KOBJS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ar libunix.a" VERBATIM)

# Link -- besm6.o first, then the archive, then -lruntime ALONE (no -lc: the kernel has its
# own printf in prf.c and calls no library routine).  Then the nm/dis postlude and the size
# print.  sh -c wraps the two redirections, which add_custom_command cannot express itself.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/unix
    COMMAND ${B6LD} ${BESM6_O} libunix.a -o unix -L${B6LIBDIR} -lruntime
    COMMAND sh -c "${B6NM} -n unix > unix.nm"
    COMMAND sh -c "${B6DISASM} unix > unix.dis"
    COMMAND ${B6SIZE} -w unix
    DEPENDS ${BESM6_O} ${CMAKE_CURRENT_BINARY_DIR}/libunix.a
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ld unix" VERBATIM)

add_custom_target(kernel ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/unix)

# `make run' boots the freshly-linked image under SIMH (distinct from the top-level
# `make run', which runs unit tests).  Prints `mem = ...' then stops at `panic: iinit'.
if(BESM6)
    add_custom_target(run
        COMMAND ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}/unix.ini
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS kernel USES_TERMINAL VERBATIM)
endif()

enable_testing()
add_subdirectory(test)
