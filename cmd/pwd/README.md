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

* **`read(file, &dir, sizeof(dir)) < sizeof(dir)` was a signed/unsigned comparison.** `read()`
  returns an `int`; `sizeof` yields an unsigned type
  ([`sys/param.h`](../../include/sys/param.h) says so, and all of libc writes `(int)sizeof`),
  so the usual arithmetic conversions promote a failed read's `-1` to 2⁴⁸−1 and the test is
  **false**. A failed read then looks like a good one: `dir` holds whatever was there before,
  and the `do … while (dir.d_ino != d.st_ino)` loop either spins on stale data or matches by
  accident and prints a wrong path. The end-of-directory `0` compares correctly either way,
  which is exactly why the common path worked and hid it.

* **`while (dir.d_name[++i] != 0);` assumed the name is terminated.** `d_name` is a bare
  `char[DIRSIZ]`; the kernel zero-*pads* a short name, so a name of exactly `DIRSIZ` characters
  fills the array with no NUL and the scan walks off the end of the struct. That was a v7 bug
  at 14 characters and was the same bug at 18. The mount-crossing branch was worse — it handed
  `d_name` straight to `stat()`, which reads past the struct for the same reason.

**Both were fixed by hand first and then stopped existing.** Task C24 put the walk on
[`opendir(3)`](../../lib/libc/man/directory.3.umm), and there is no `read()` left to get the
comparison wrong and no unterminated name to bound: `readdir()` returns `NULL` at the end and
`d_namlen` with each name. `d_ino` is what `pwd` matches on and `struct dirent` carries it, so
the algorithm is exactly v7's. Three smaller things followed:

* `fstat(file, &dd)` — the mount-point test — became `fstat(dirfd(dirp), &dd)`.
* `cat()` takes the component and its length as arguments instead of reading a file-scope
  `struct direct`. It has to: `readdir()`'s return points into the stream's own storage, so it
  is called **before** the `closedir()`, where the old code ran after the `close()`.
* The mount branch `stat()`s `dp->d_name` directly, the terminated-copy buffer being gone.

**`DIRSIZ` is 18 here, not v7's 14.** Nothing in this file hardcoded it, so the only
consequence is that the 512-character path buffer holds about 27 components instead of 34; the
two magic `511`s are one `NAMEBUF` constant. The cost of the conversion was **212 words**,
4,100 to 4,312.

Not testable under `b6sim`, for the same reason [`cmd/ls`](../ls/README.md) is not: it reads
directories, and `b6sim` maps `read()` onto the host, where a directory descriptor refuses to
be read. `opendir(3)` does not rescue that and makes it worse — on the host `open(2)` and
`fstat(2)` on a directory both succeed and only `read(2)` refuses, so every directory reads as
**empty**, indistinguishably from a real empty one. So `pwd` gets its size check
(`rootfs_pwd_size`, label `rootfs`) and nothing else; `kernel/test/console`, which typed `pwd`
at a booted shell and expected `/`, was deleted with the other seventeen typed dialogues.

`pwd.1.umm` is the v7 manual page, kept as it was.
