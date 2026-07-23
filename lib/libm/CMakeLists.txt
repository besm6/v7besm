# libm for programs running under this kernel.  See ../README.md.
#
# The v7 math library refitted to the BESM-6's one 48-bit floating format.  One product,
# libm.a; a program that uses it links -lm, placed ahead of -lc (libm calls errno, frexp,
# ldexp and modf in libc; libc calls nothing back).  All sources are flat .c here.
set(LIBM_OBJ
    fabs floor trunc round copysign fmin fmax fma fmod
    sqrt hypot pow exp log
    sin tan asin atan sinh tanh
    erf j0 j1 jn)

set(OBJS "")
set(OBJ_NAMES "")
foreach(_n ${LIBM_OBJ})
    b6_obj(_o ${CMAKE_CURRENT_SOURCE_DIR}/${_n}.c)
    list(APPEND OBJS ${_o})
    list(APPEND OBJ_NAMES ${_n}.o)
endforeach()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libm.a
    COMMAND ${B6AR} cr libm.a ${OBJ_NAMES}
    COMMAND ${B6RANLIB} libm.a
    DEPENDS ${OBJS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ar libm.a" VERBATIM)

add_custom_target(libm ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libm.a)

# Install beside libc.a and libruntime.a; install() preserves the timestamp for the same
# __.SYMDEF reason libc/CMakeLists.txt gives.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libm.a DESTINATION share/besm6/lib)
