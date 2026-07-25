/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sync -- flush the buffer cache to disk.
//
// The v7 program, unchanged in what it does: it calls sync(2) and stops.  It is the
// smallest command on the system -- smaller than echo, since it has no arguments, no
// output and no error case -- and the port is the mechanical C11 pass and nothing else.
// v7 wrote `main() { sync(); }' and let both the return type and the declaration of
// sync() default to `int'; b6parse has neither implicit `int' nor an implicit function
// declaration, so main() gets its prototype and <unistd.h> supplies sync()'s.
//
// `return 0' rather than a bare fall-off: crt0 calls exit(main(argc, argv)), so what main
// leaves in the accumulator IS the exit status, and a v7 main that ran off its closing
// brace exited with whatever happened to be there.
//
// WHY THIS COMMAND EXISTS HERE AND NOW.  There is no update(8) on this system -- nothing
// flushes the cache on a timer -- so a session that writes files leaves them in delayed-
// write buffers until something forces them out.  kernel/test/session is the test that
// cares: it writes, runs this, and then fscks the container to prove that what the kernel
// said it wrote is what the disk actually holds (kernel/TODO.md, task 25b).
//
#include <unistd.h>

int main(void)
{
    sync();
    return 0;
}
