# `rev` reverses characters, and v7's reversed bytes

`/bin/rev` is one of task C5a's six ([../TODO.md](../TODO.md)) and the only one of them that
is **not a faithful port**. [../README.md](../README.md) §10 allows a divergence and asks for
it to be written down twice — in the source and in the manual page; this file is the third
place, because the divergence is about something bigger than `rev`.

## What was decided

**v7's `rev` reverses bytes.** That is the same thing as reversing characters on a machine
whose text is ASCII, and it is not the same thing here. Since task C11 this system's text is
**UTF-8 end to end** ([../README.md](../README.md) §11): the console driver is byte-transparent
in both directions, so are the clists and the filesystem, and `/bin/sh` stopped marking a
quoted character with bit `0200`, so a Cyrillic string survives being typed, being written,
being stored, being globbed and being passed as an argument.

Against that, a byte-reversing `rev` is the one program on the image that takes a line the
machine can now handle everywhere else and returns something that is not text:

```
$ echo привет мир | rev          # bytes:      <12 bytes of mojibake>
$ echo привет мир | rev          # characters: рим тевирп
```

So it reverses **UTF-8 sequences**. The rule needs no table and no library: a byte
`10xxxxxx` is a continuation and belongs to the byte that leads it, so the reversal walks
backwards, collects each character's bytes into a group, and emits the group **forwards**.
Twenty lines, no `wchar_t`, no locale.

## Three things that fall out of it, and the third is the interesting one

**Malformed input still loses nothing.** A continuation byte with no lead comes out as a
group of its own, so `rev` remains safe on arbitrary bytes — it stops treating them all
*alike*, but it never drops one and never reorders one out of its group. That property is
what makes the divergence acceptable in a filter: `rev | rev` is still the identity on any
input, valid UTF-8 or not.

**A `char` is unsigned here, so the test is the obvious one.** `(line[k] & 0300) == 0200`
reads a continuation byte as a value in `0200..0277` rather than as a negative number. On a
signed-`char` machine the same line would need a cast; [../README.md](../README.md) §3 says
this is habit rather than necessity here, and this is one of the places where the habit would
have been the only thing standing between the code and a sign extension.

**The long-line split had to learn about characters too, and this is the part that would have
been easy to leave broken.** v7 cuts a line longer than its buffer into buffer-sized pieces
and reverses each on its own. With bytes that is arbitrary but harmless. With characters, a
cut lands wherever the buffer happens to end — in the middle of a two-byte letter as often as
not — and both pieces come out mangled, so the program would reverse characters correctly on
every line except the ones long enough to need a buffer at all. The cut **backs off to a
sequence boundary** and the bytes it declines to take begin the next piece.

That is the general lesson and it is not about UTF-8: **when a program is taught to respect a
structure, every place it chops the input has to learn the same thing.** The reversal is
where a reader looks; the buffer boundary is where the bug would have lived. `cmd/rev/test`'s
`long` case exists for exactly that boundary — 1023 bytes of `a` followed by `привет`, so
that the 1024th byte is the *lead* byte of `п` and the incomplete sequence has to be carried
forward. Nothing shorter than the buffer can assert it.

## And one upstream bug, which is not a divergence

v7's `case EOF: goto eof` abandons whatever is already in the buffer, so a final line with no
newline is **discarded**: `printf abc | rev` printed nothing. It prints `cba` now.
[../README.md](../README.md)'s rule is that upstream bugs are fixed rather than carried and
that the fix says which it is — this one is a bug, and the character reversal above is a
decision. Keeping them apart in the source header and in `rev.1` is the whole point of
stating both.

## What was left alone

Two things a reader might take for bugs and neither is:

* **A file that cannot be opened ends the run**, rather than the remaining arguments being
  read. `wc` in the same task was changed the other way — it skips and exits 1 — because `wc`
  is a *summary* over files and a missing one leaves the rest meaningful, while `rev` is a
  concatenation and a hole in the middle of it is worse than no output. Both are stated in
  their pages.
* **The no-argument path `fclose`s `stdin`.** It is the last thing the program does.

## Sizes

`rev` is 4,428 words of the 28,672 ([../README.md](../README.md) §6), of which 1,204 are bss
— the 1024-byte line buffer at file scope plus stdio's. The buffer went from v7's 256 bytes
to 1024 while the code was being touched: it costs bss and not the four-page stack, which is
the ceiling nothing checks.
