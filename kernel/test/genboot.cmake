# Generate a .ini from a .ini.in, substituting each kernel symbol the script names with its
# linked address, read out of unix.nm.  Run at build time (the addresses are link-time values
# that shift with the kernel), as:
#   cmake -DNM=<unix.nm> "-DSYMS=a;b;c" -DIN=<x.ini.in> -DOUT=<x.ini> -P genboot.cmake
#
# Two consumers.  boot.ini wants `spin' (kernel/besm6.S), the icode's failure loop -- where
# process 1 parks when exec("/etc/init") fails; boot.ini used to ASSERT that state, back when
# the image carried no /etc/init, and the breakpoint is kept to diagnose it.  swap.ini wants
# `phymem', which it DEPOSITS a smaller value into to squeeze the coremap, and the swapper's
# traffic counters, which it asserts are non-zero afterwards.
#
# b6nm prints "ADDR t <name>" in octal and we take ADDR verbatim, so both the .ini's octal
# `if (PC != <addr>)' and its `deposit <addr> <octal>' compare and write like with like.

if(NOT EXISTS "${NM}")
    message(FATAL_ERROR "genboot: ${NM} does not exist -- the kernel image must be linked first")
endif()

set(_report "")
if(NOT DEFINED SYMS)
    message(FATAL_ERROR "genboot: no SYMS given")
endif()

foreach(_sym ${SYMS})
    file(STRINGS "${NM}" _lines REGEX "[ \t]${_sym}$")
    list(LENGTH _lines _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "genboot: expected exactly one `${_sym}' in ${NM}, found ${_n}")
    endif()
    list(GET _lines 0 _line)
    string(TOUPPER "${_sym}" _var)
    string(REGEX MATCH "^[0-7]+" ${_var} "${_line}")
    if(${_var} STREQUAL "")
        message(FATAL_ERROR "genboot: could not parse an octal address from '${_line}'")
    endif()
    string(APPEND _report " ${_sym}=${${_var}}")
endforeach()

configure_file("${IN}" "${OUT}" @ONLY)
message(STATUS "boot.ini:${_report}")
