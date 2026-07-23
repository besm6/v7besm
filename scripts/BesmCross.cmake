# Shared BESM-6 cross-toolchain resolution for the CMake builds that DON'T use the host
# compiler -- the kernel and the user libraries.  Both emit BESM-6 a.out through the b6*
# tools rather than through CMake's C-language support, and both need the same things: the
# tools themselves, the external compiler's libruntime.a, and the two compile/find helpers.
# This file is that common half, so it lives in one place and is included by
#   - the top-level CMakeLists.txt (once, inside the libruntime guard), whence its variables
#     and functions are inherited by add_subdirectory(kernel)/add_subdirectory(lib); and
#   - a standalone `cmake -S kernel' or `cmake -S lib', which has no parent to inherit from
#     and so includes it directly (guarded by `if(NOT COMMAND b6_obj)').
# include_guard is belt-and-suspenders against a second include reaching here.
include_guard(GLOBAL)

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
    set(B6DISASM $<TARGET_FILE:b6disasm>)
    set(B6SIM    $<TARGET_FILE:b6sim>)
    set(B6CC_ENV ${CMAKE_COMMAND} -E env
        B6CPP=$<TARGET_FILE:b6cpp> B6AS=$<TARGET_FILE:b6as> B6LD=$<TARGET_FILE:b6ld>)
else()
    find_program(B6CC     b6cc     REQUIRED)
    find_program(B6AS     b6as     REQUIRED)
    find_program(B6LD     b6ld     REQUIRED)
    find_program(B6AR     b6ar     REQUIRED)
    find_program(B6RANLIB b6ranlib REQUIRED)
    find_program(B6NM     b6nm     REQUIRED)
    find_program(B6SIZE   b6size   REQUIRED)
    find_program(B6DISASM b6disasm REQUIRED)
    find_program(B6SIM    b6sim    REQUIRED)
    set(B6CC_ENV "")
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
