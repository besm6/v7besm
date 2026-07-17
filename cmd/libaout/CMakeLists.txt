#
# Library of helpers to serialize/deserialize BESM-6 a.out object files.
# Built from the host C compiler: it runs on the host and manipulates BESM-6
# objects.  Shared by as, ld, ar, nm, size and the rest of the toolchain.
#
# The fd-based ar/ranlib helpers (getint.c, putint.c, getarhdr.c, putarhdr.c)
# keep their file-descriptor interface for in-place archive rewrites, but now
# share the BESM-6 half-word on-disk layout used by the fget*() routines.
#
add_library(aout STATIC
    arhdrsz.c
    fgetarhdr.c
    fgethdr.c
    fgeth.c
    fgetw.c
    fgetint.c
    fgetran.c
    fgetsym.c
    fputhdr.c
    fputh.c
    fputw.c
    fputran.c
    fputsym.c
    getarhdr.c
    getint.c
    putarhdr.c
    putint.c
    shortaddr.c
)
target_include_directories(aout PUBLIC ${BESM6_CROSS_INCLUDE})

add_subdirectory(test)
