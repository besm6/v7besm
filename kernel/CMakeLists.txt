# Unix v7 kernel for the BESM-6.
#
# Cross-compiled by the b6* toolchain (b6cc/b6as/b6ld/b6ar/b6ranlib/b6nm/b6size/b6disasm),
# which emit BESM-6 a.out -- not host objects -- so this is a `project(... NONE)' build that
# drives the tools through custom commands rather than CMake's C-language support.  Integrated,
# it uses the in-tree tool targets; standalone (`cmake -S kernel') it falls back to the
# installed tools -- see scripts/BesmCross.cmake.  Its counterpart is `lib/', which shares that
# module and the same in-tree-vs-installed toolchain story.
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

# The cross toolchain, the external runtime, and the b6_obj/b6_find_src helpers all come from
# the shared module.  Integrated, the top-level project already included it (its variables and
# functions are inherited here); standalone (`cmake -S kernel'), no parent did, so include it
# now -- `if(NOT COMMAND b6_obj)' is the "were we included by a parent?" test.
if(NOT COMMAND b6_obj)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../scripts/BesmCross.cmake)
endif()

# The one system-header tree, and a coarse dependency on all of sys/: b6cc has no -M, so
# every object simply depends on every system header (the same trade the test dir makes).
# Absolute, from CMAKE_CURRENT_SOURCE_DIR so it is right whether standalone or integrated.
get_filename_component(KINC ${CMAKE_CURRENT_SOURCE_DIR}/../include ABSOLUTE)
file(GLOB KHDRS ${KINC}/sys/*.h)

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
# `make run', which runs unit tests).  Prints the memory sizes and the root size, then
# runs on in user mode -- see unix.ini.
if(BESM6)
    add_custom_target(run
        COMMAND ${BESM6} ${CMAKE_CURRENT_SOURCE_DIR}/unix.ini
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS kernel USES_TERMINAL VERBATIM)
endif()

enable_testing()
add_subdirectory(test)

# unix.ini attaches test/root3072.disk, so `make run' needs the root image built as well as
# the kernel.  The target comes from test/, hence the dependency here rather than above.
if(BESM6)
    add_dependencies(run rootimg)
endif()
