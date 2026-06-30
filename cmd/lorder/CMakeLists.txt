#
# lorder: topological ordering helper for the BESM-6 archiver/linker.
# A shell script — installed, not compiled.
#
install(PROGRAMS lorder.sh DESTINATION bin RENAME b6lorder)

add_subdirectory(test)
