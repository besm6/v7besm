# Generate boot.ini from boot.ini.in, substituting @BHALT@ with the linked address of the
# kernel's `bhalt' symbol read out of unix.nm.  Run at build time (the address is a link-time
# value that shifts with the kernel), as: cmake -DNM=<unix.nm> -DIN=<boot.ini.in> -DOUT=<boot.ini> -P genboot.cmake
#
# bhalt is where process 1 halts on return from main() (kernel/besm6.S); boot.ini asserts the
# boot reaches it.  b6nm prints "ADDR t bhalt" in octal; we take ADDR verbatim, so the .ini's
# octal `if (PC != <addr>)' compares like with like.

if(NOT EXISTS "${NM}")
    message(FATAL_ERROR "genboot: ${NM} does not exist -- the kernel image must be linked first")
endif()

file(STRINGS "${NM}" _lines REGEX "[ \t]bhalt$")
list(LENGTH _lines _n)
if(NOT _n EQUAL 1)
    message(FATAL_ERROR "genboot: expected exactly one `bhalt' in ${NM}, found ${_n}")
endif()

list(GET _lines 0 _line)
string(REGEX MATCH "^[0-7]+" BHALT "${_line}")
if(BHALT STREQUAL "")
    message(FATAL_ERROR "genboot: could not parse an octal address from '${_line}'")
endif()

configure_file("${IN}" "${OUT}" @ONLY)
message(STATUS "boot.ini: bhalt=${BHALT}")
