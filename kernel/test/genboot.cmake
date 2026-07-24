# Generate boot.ini from boot.ini.in, substituting each kernel symbol the script asserts on
# with its linked address, read out of unix.nm.  Run at build time (the addresses are
# link-time values that shift with the kernel), as:
#   cmake -DNM=<unix.nm> -DIN=<boot.ini.in> -DOUT=<boot.ini> -P genboot.cmake
#
# One symbol today: `spin' (kernel/besm6.S), the icode's failure loop -- where process 1 parks
# when exec("/etc/init") fails, which is the state boot.ini asserts (there is no /etc/init
# until task 24).  The loop below is a list so a second one costs a word, not a rewrite.
#
# b6nm prints "ADDR t <name>" in octal and we take ADDR verbatim, so the .ini's octal
# `if (PC != <addr>)' compares like with like.

if(NOT EXISTS "${NM}")
    message(FATAL_ERROR "genboot: ${NM} does not exist -- the kernel image must be linked first")
endif()

set(_report "")
foreach(_sym spin)
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
