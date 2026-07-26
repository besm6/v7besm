# The library test programs that go on the root disk image, as /usr/test/<name>.
#
# ONE LIST, TWO CONSUMERS, and they are configured in different directories: this one
# (lib/test/CMakeLists.txt) links and stages them, and kernel/test/CMakeLists.txt names
# them in ROOTFS_FILES -- the dependency list that makes editing a test source rebuild
# root.img -- and registers the ctest case that diffs each program's output after the
# kernel has run it.  A third consumer, ../../root.manifest, is a static file and says the
# same names by hand; nothing but the manifest's own grammar can be generated for it.
#
# THIS IS NOT THE SAME LIST as the b6_libtest() calls next door, and deliberately.  Two
# programs are in one world only:
#   spawn   runs under b6sim ONLY.  Its whole premise is that /bin/sh cannot be exec'd,
#           which is true on the host and false on the image.
#   shellt  runs on the image ONLY, and is the other half of that: system() and popen()
#           with a shell that really does start.
# So `spawn' is absent below and `shellt' is present, and the count stays at 21 either way.
#
# The order is the order kernel/test/libtest.sh runs them in, which is the order
# lib/test/CMakeLists.txt registers them in: roughly the order libc was built up, so a
# failure early in the list is a failure in something everything after it depends on.
set(B6_LIBTEST_IMAGE
    hello vararg errno procs sbrkt malloct strings gen strtolt environ jmp headers
    stdiot printft scanft execs shellt timet pwent signals matht puret)
