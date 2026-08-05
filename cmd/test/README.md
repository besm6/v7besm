# `test`, and the first hard link on the image

Task C2b, and the program the task exists for: this shell has no built-in `test`, so before
`/bin/test` was on the image nothing on this machine could branch. The C11 pass over
[test.c](test.c) is described in its own header and is not repeated here. Two things the port
*taught* are, because neither is about this program alone.

## One inode, two names

v7 links the binary a second time as `[`, and `main()`'s first act is to look at `argv[0]`:

```c
if (EQ(argv[0], "[")) {
    if (!EQ(argv[--ac], "]"))
        synbad("] missing", "");
}
```

Without the second name that branch is unreachable code and `[ -f x ]` in a script is a
command not found. So the link is part of the port, not a convenience — and it is the **first
hard link [../../root.manifest](../../root.manifest) has ever carried**:

```
link /bin/[
target /bin/test
```

Three things follow, and they are the reusable half of this task.

**A `link` stanza names no `source` and no `mode`.** There is nothing to copy in and nothing
to set: the inode is the other entry's already. `b6fsutil` resolves links last
([cmd/fsutil/manifest.cpp](../fsutil/manifest.cpp)), so the order in the manifest does not
matter.

**Nothing else in the build knows about it.** `build/rootfs/` holds one file, `bin/test`;
`ROOTFS_FILES` in [../../kernel/test/CMakeLists.txt](../../kernel/test/CMakeLists.txt) lists
one entry; `b6_prog()` registers one size check. A link is invisible to every one of them, so
a manifest edit that dropped it would break nothing that any existing test could see.

**Hence `rootimg_link`**, beside `rootimg_setuid` and for exactly the same reason — the
finished image is the only thing that can answer. It asserts two facts out of
`b6fsutil -v -v`: both paths report `nlink 2`, and *everything before the path is identical
between them* — which is type, mode, owner, size and i-number, so the second is what really
says they are one file. `rootimg_check`'s five-pass fsck validates the link count
independently, and `kernel/test/utils.sh` types `[ ... ]` at the shell, which is the only
place the `] missing` diagnostic is reachable at all.

## An exit status of 255 is not observable

`test` reports a syntax error with `exit(255)`, and **no caller on this machine sees that
number**. A wait status is `(code << 8)` and it comes back through r12, a fifteen-bit index
register ([../../lib/libc/sys/wait.S](../../lib/libc/sys/wait.S) states it; `await()` in
[../sh/service.c](../sh/service.c) repeats it), so `255 << 8` loses its top bit and the
shell's `$?` reads **127**.

That is why `kernel/test/utils.expected` says `bracket unterminated status 127` while the
`cmd_test_*` cases next door, where the *host* does the waiting, really do assert 255. Two
harnesses, two answers, one program — as with `tty`, and for a reason each states.

**It is left at 255 rather than lowered to something visible.** The truncation is the
system's ABI and reaches every program that exits above 127; moving this one number would
hide a general limitation behind a local divergence, and `exit(2)` really does receive the
255. [test.1.umm](test.1.umm) records the effect where a user meets it. Widening it means giving the
`wait` gate an argument and writing the status through the caller's pointer kernel-side, which
`lib/README.md` already names as future work.

## What did *not* need saying

The rest is ordinary. `test.c` has no `long`, no `%D` and no buffer of
any kind; at 964 words it is the smallest program on the image, because its whole output is
four `write(2)` calls and it links no stdio at all. `exp()` had to be renamed away from
`<math.h>`'s, which is §1's standing warning and not a new finding.
