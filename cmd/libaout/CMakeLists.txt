#
# Library of helpers to serialize/deserialize BESM-6 a.out object files.
# Built from the host C compiler: it runs on the host and manipulates BESM-6
# objects.  Shared by as, ld, ar, nm, size and the rest of the toolchain.
#
# The K&R-style sources fputran.c, getarhdr.c, getint.c, putarhdr.c and
# putint.c are not yet ported to the besm6/ cross headers and ANSI prototypes,
# so they are left out until modernized.
#
add_library(aout STATIC
    fgetarhdr.c
    fgethdr.c
    fgeth.c
    fgetint.c
    fgetran.c
    fgetsym.c
    fputhdr.c
    fputh.c
    fputsym.c
)
target_include_directories(aout PUBLIC ${BESM6_CROSS_INCLUDE})

add_subdirectory(test)
