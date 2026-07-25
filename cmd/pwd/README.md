# pwd — Unix v7 `pwd(1)` for the BESM-6

`/bin/pwd`, staged as **`build/rootfs/bin/pwd`** by one `b6_prog()` call in
[`CMakeLists.txt`](CMakeLists.txt). Like [`cmd/init/`](../init/) and [`cmd/sh/`](../sh/), this
is a `cmd/` subdirectory that is **not a host tool**. 4,069 words of the 28,672.

It is the first of the four commands that reads the **filesystem's own format** rather than a
stream of bytes, and that is what makes it interesting. There is no `getcwd()` in v7 and no way
to ask the kernel where you are, so `pwd` walks *up*, one `..` at a time, reading each parent
directory and looking for the entry whose inode number matches the child it just came from —
and building the path backwards as it goes.

Beyond the mechanical C11 pass (prototypes, explicit return types, `static`, `O_RDONLY` for
v7's bare `0`), the port carries two fixes. Both are latent bugs in the v7 source that this
machine makes live:

* **`read(file, &dir, sizeof(dir)) < sizeof(dir)` is a signed/unsigned comparison.** `read()`
  returns an `int`; `sizeof` yields an unsigned type
  ([`sys/param.h`](../../include/sys/param.h) says so, and all of libc writes `(int)sizeof`),
  so the usual arithmetic conversions promote a failed read's `-1` to 2⁴⁸−1 and the test is
  **false**. A failed read then looks like a good one: `dir` holds whatever was there before,
  and the `do … while (dir.d_ino != d.st_ino)` loop either spins on stale data or matches by
  accident and prints a wrong path. The end-of-directory `0` compares correctly either way,
  which is exactly why the common path works and hides it. Cast, at both sites.

* **`while (dir.d_name[++i] != 0);` assumes the name is terminated.** `d_name` is a bare
  `char[DIRSIZ]`; the kernel zero-*pads* a short name, so a name of exactly `DIRSIZ` characters
  fills the array with no NUL and the scan walks off the end of the struct. That was a v7 bug
  at 14 characters and is the same bug at 18. Both scans are bounded now, and the
  mount-crossing branch — which hands `d_name` straight to `stat()`, which would read past the
  struct for the same reason — copies into a terminated buffer first.

**`DIRSIZ` is 18 here, not v7's 14**, and a `struct direct` is four words with a full-word
`d_ino`. Nothing in this file hardcoded either number, so the only consequence is that the
512-character path buffer now holds about 27 components instead of 34; the two magic `511`s are
one `NAMEBUF` constant.

Not testable under `b6sim`, for the same reason [`cmd/ls`](../ls/README.md) is not: it reads
directories, and `b6sim` maps `read()` onto the host, where a directory descriptor refuses to
be read. It gets its size check (`rootfs_pwd_size`, label `rootfs`) and is exercised under the
real kernel by [`kernel/test/console`](../../kernel/test/console.ini), which types `pwd` at the
shell and expects `/`.

`pwd.1` is the v7 manual page, kept as it was.
