#
# The libaout source list, named once so the host library and the NATIVE builds
# of the toolchain cannot drift apart.
#
# b6_prog() takes a flat SOURCES list and there is no native archive to link
# against, so every program built for the BESM-6 that touches an a.out has to
# name these files itself.  Six do: cmd/as/rootfs and cmd/ld/rootfs (task C9b),
# and cmd/{nm,size,strip,disasm}/rootfs (task C9c).  ar and ranlib will be the
# first to want the fd flavour below as well.
#
# Read with a path relative to the includer, exactly as lib/test/progs.cmake is.
#
set(_LIBAOUT_DIR ${CMAKE_CURRENT_LIST_DIR})

# The FILE* flavour, plus the two half-word primitives and the short-address
# codec: everything an assembler or a linker reads and writes.
set(B6_LIBAOUT_SOURCES_NATIVE
    ${_LIBAOUT_DIR}/arhdrsz.c
    ${_LIBAOUT_DIR}/fgetarhdr.c
    ${_LIBAOUT_DIR}/fgethdr.c
    ${_LIBAOUT_DIR}/fgeth.c
    ${_LIBAOUT_DIR}/fgetw.c
    ${_LIBAOUT_DIR}/fgetint.c
    ${_LIBAOUT_DIR}/fgetran.c
    ${_LIBAOUT_DIR}/fgetsym.c
    ${_LIBAOUT_DIR}/fputhdr.c
    ${_LIBAOUT_DIR}/fputh.c
    ${_LIBAOUT_DIR}/fputw.c
    ${_LIBAOUT_DIR}/fputran.c
    ${_LIBAOUT_DIR}/fputsym.c
    ${_LIBAOUT_DIR}/shortaddr.c)

# ...and the file-descriptor flavour, which exists for ar/ranlib's in-place
# archive rewrites.  None of the six above calls any of it -- nm reads an archive
# header through the FILE* fgetarhdr() -- and b6_prog() links every object it is
# handed whether it is referenced or not, so these four stay out of a native
# program until a native ar wants them.
set(B6_LIBAOUT_SOURCES
    ${B6_LIBAOUT_SOURCES_NATIVE}
    ${_LIBAOUT_DIR}/getarhdr.c
    ${_LIBAOUT_DIR}/getint.c
    ${_LIBAOUT_DIR}/putarhdr.c
    ${_LIBAOUT_DIR}/putint.c)
