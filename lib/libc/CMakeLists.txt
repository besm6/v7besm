# libc for programs running under this kernel.  See ../README.md.
#
# Two products: the archive libc.a, and crt0.o beside it rather than in it -- a program's
# startup is named on the link line, never pulled by symbol (where v7 keeps it too,
# /lib/crt0.o).  The source directories are the v7 ones (csu, sys, gen, stdio); the objects
# all land flat in the binary dir, and one b6ranlib index lets a member calling back into
# another resolve in a single scan.

# Resolve an object's source by basename across the v7 source directories -- the CMake stand-in
# for the old Makefile's suffix-scoped vpath.  file(GLOB) returns the entry's REAL case, so on a
# case-insensitive filesystem a .S source (needs cpp) is still told from a .s (straight to as).
function(libc_src outvar base)
    foreach(dir sys gen stdio csu)
        file(GLOB _cands "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/${base}.*")
        foreach(cand ${_cands})
            get_filename_component(_ext ${cand} EXT)
            if(_ext STREQUAL ".c" OR _ext STREQUAL ".s" OR _ext STREQUAL ".S")
                set(${outvar} ${cand} PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
    message(FATAL_ERROR "libc_src: no source for '${base}'")
endfunction()

# The syscall leaves.  Most are GENERATED -- one .S and one object per line of
# sys/syscalls.tbl, so b6ranlib's index lets b6ld pull only what a program calls.  STUBS is the
# first column of every non-comment line; the generated .S land in the binary dir (a leaf names
# its syscall as the SYS_* macro of <sys/syscall.h>, which cpp resolves -- so no number appears
# anywhere under lib/).  Adding a syscall is adding a line to the table and nothing else.
set(TBL    ${CMAKE_CURRENT_SOURCE_DIR}/sys/syscalls.tbl)
set(MKSTUB ${CMAKE_CURRENT_SOURCE_DIR}/sys/mkstub)
# The first column of every non-comment line, exactly as the old Makefile read it
# (`sed -e 's/#.*//' | awk 'NF { print $1 }'').  Reuse that pipeline rather than parse in CMake:
# a comment here can carry a `;', and CMake's file(STRINGS) would split a line on it before the
# comment is stripped.  sed removes the comment (and its `;') first, so the field is clean.
execute_process(
    COMMAND sh -c "sed -e 's/#.*//' ${TBL} | awk 'NF { print $1 }'"
    OUTPUT_VARIABLE _stubs_raw
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "\n" ";" STUBS "${_stubs_raw}")

set(OBJS "")        # absolute object paths, for DEPENDS
set(OBJ_NAMES "")   # bare basenames, for the b6ar command line (objects are flat in the bindir)

# Generate and compile each stub.  mkstub reads the table by absolute path, so the redirection
# (which add_custom_command cannot express) is wrapped in sh -c.
foreach(_s ${STUBS})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_s}.S
        COMMAND sh -c "${MKSTUB} ${_s} ${TBL} > ${CMAKE_CURRENT_BINARY_DIR}/${_s}.S"
        DEPENDS ${TBL} ${MKSTUB}
        COMMENT "mkstub ${_s}.S" VERBATIM)
    b6_obj(_o ${CMAKE_CURRENT_BINARY_DIR}/${_s}.S)
    list(APPEND OBJS ${_o})
    list(APPEND OBJ_NAMES ${_s}.o)
endforeach()

# The hand-written leaves the generator cannot produce (a second result in r12, an argument that
# must survive the trap, or no return on success), the C exec wrappers, and sbrk -- SYS in the
# old Makefile; then GEN (strings, ctype, setjmp, malloc, the conversions, the date family, ...)
# and STDIO (the whole FILE machinery).  Names transcribed from libc/Makefile; libc_src maps
# each to its source and extension.
set(LIBC_OBJ
    # SYS (hand-written)
    cerror exit fork wait pipe getpid getuid getgid time dup exec exece
    execl execv execle sbrk
    # GEN
    strcat strcmp strcpy strlen strncat strncmp strncpy strchr strrchr strstr strtok
    memcpy memmove memset memcmp memchr
    abs atoi atol index rindex swab qsort rand
    getenv mktemp isatty sleep perror errlst ctype_ abort raise
    setjmp assert toupper tolower isblank
    malloc calloc aligned_alloc cuexit
    atof ecvt modf frexp ldexp
    execvp execlp ttyname ttyslot getlogin tell crypt
    ctime clock difftime mktime strftime timezone
    # STDIO
    data findiop endopen fopen fdopen freopen filbuf flsbuf
    fseek ftell rew fgetpos fsetpos
    setbuf setvbuf clrerr feof ferror
    fgetc fputc getc putc getchar putchar ungetc
    fgets fputs gets puts fread fwrite getw putw
    remove rename tmpnam tmpfile
    doprnt printf fprintf sprintf snprintf
    vprintf vfprintf vsprintf vsnprintf
    doscan scanf fscanf sscanf vscanf vfscanf vsscanf
    gcvt
    system popen
    getpwent getpwnam getpwuid getgrent getgrnam getgrgid
    getpw getpass)

foreach(_n ${LIBC_OBJ})
    libc_src(_src ${_n})
    b6_obj(_o ${_src})
    list(APPEND OBJS ${_o})
    list(APPEND OBJ_NAMES ${_n}.o)
endforeach()

# crt0.o: built like the rest but NOT archived -- named on the link line instead.
libc_src(_crt0 crt0)
b6_obj(CRT0_O ${_crt0})

# libc.a -- one link-pulled archive with a b6ranlib index.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libc.a
    COMMAND ${B6AR} cr libc.a ${OBJ_NAMES}
    COMMAND ${B6RANLIB} libc.a
    DEPENDS ${OBJS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ar libc.a" VERBATIM)

add_custom_target(libc ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libc.a ${CRT0_O})

# Install beside the c-compiler's libruntime.a in share/besm6/lib, where b6cc's find_crt0()
# names crt0.o outright and add_default_libdirs() puts a -L for the implicit -lc.  install()
# preserves the source timestamps, which matters: b6ld calls a randomized archive out of date
# when its mtime is newer than the date in __.SYMDEF (cmd/ld/library.c), and this keeps
# b6ranlib's index trustworthy where it lands (the old Makefile's `cp -p').
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libc.a ${CRT0_O}
        DESTINATION share/besm6/lib)
