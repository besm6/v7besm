# libtermcap for programs running under this kernel.  See README.md and ../README.md.
#
# The 4.xBSD terminal-capability library -- tgetent/tgetnum/tgetflag/tgetstr, tgoto and
# tputs -- reading the /etc/termcap that etc/ stages onto the root filesystem.  One
# product, libtermcap.a; a program that uses it links -ltermcap AHEAD of -lc, because
# termcap calls getenv, open, read, close, write and the string routines and libc calls
# nothing back.  That is the same one-scan contract libm obeys (lib/README.md).
#
# tcattr.c of the upstream directory is not here: it is a termios shim wrapped entirely in
# `#if 0', and this system has sgtty, not termios.
#
# No -DCM_N/-DCM_GT/-DCM_B/-DCM_D as the upstream Makefile passed: tgoto.c compiles all
# four % forms unconditionally, and says why.
set(LIBTERMCAP_OBJ termcap tgoto tputs)

set(OBJS "")
set(OBJ_NAMES "")
foreach(_n ${LIBTERMCAP_OBJ})
    b6_obj(_o ${CMAKE_CURRENT_SOURCE_DIR}/${_n}.c)
    list(APPEND OBJS ${_o})
    list(APPEND OBJ_NAMES ${_n}.o)
endforeach()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libtermcap.a
    COMMAND ${B6AR} cr libtermcap.a ${OBJ_NAMES}
    COMMAND ${B6RANLIB} libtermcap.a
    DEPENDS ${OBJS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ar libtermcap.a" VERBATIM)

add_custom_target(libtermcap ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libtermcap.a)

# Install beside libc.a, libm.a and libruntime.a; install() preserves the timestamp for the
# same __.SYMDEF reason libc/CMakeLists.txt gives.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libtermcap.a DESTINATION share/besm6/lib)
