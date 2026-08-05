# `cmd/man2umm` — roff `-man` to Unix Manual Markdown

The format is [`doc/Manual_Page_Format.md`](../../doc/Manual_Page_Format.md). This tool converts a
page into it, and then holds the result to it.

**It is not a run-once tool.** The pages this tree already carried were the first job, not the only
one: every v7 program still to be ported ([`../TODO.md`](../TODO.md) C10–C24, and the unported
pages already sitting in [`../awk/`](../awk/), [`../make/`](../make/) and the rest) arrives with a
roff page that has to become a `.umm`. §"The procedure" below is how.

It is a **host** tool and only a host tool — no `rootfs/` subdirectory, nothing runs it on the
BESM-6, and nothing could: `b6cc` drives `b6parse`, which is strict C11, and there is no C++
cross-compiler for this machine. [`../sim/`](../sim/) and [`../fsutil/`](../fsutil/) are C++ for the
same reason this is.

## The procedure

```sh
b6man2umm -o cmd/foo/foo.1.umm cmd/foo/foo.1     # convert
b6man2umm -l cmd/foo/foo.1.umm                   # canonical shape, section 9
python3 scripts/mancheck.py cmd/foo/foo.1 cmd/foo/foo.1.umm   # agree with groff?
```

Then read it, fix what the converter could not, and delete the roff. The middle step is a permanent
`ctest` (`man_lint_<page>`); the third is the one that needs the host's `groff` and is therefore
run by hand.

Then **re-configure and add a stanza to [`../../root.manifest`](../../root.manifest)**: the page
goes onto the image as `/usr/man/man<N>/<name>.<section>`, and the glob that stages it
(`B6_STAGE_MAN`, the top-level `CMakeLists.txt`) runs at configure time while the manifest is kept
by hand. See [`../README.md`](../README.md) §10.

**What always needs a human.** The converter is mechanical and its output is a draft:

- **`.HP`, the hanging paragraph.** It is a definition list wearing a disguise, and which part is
  the term is a judgment. The converter emits a line block and warns.
- **A table.** `.ta` sets tab stops a typesetter evaluates; here a tab becomes spaces on a fixed
  grid, so a column table that was aligned by `\w'text'u` widths comes out ragged. Move it into a
  fenced block and lay it out by hand.
- **`\z`, `\v`, `\c`** — overstrike, vertical motion and line continuation. Six sites in the corpus
  and every one wants a different answer.
- **An undefined `\*`.** nroff renders one as nothing, and two pages relied on that by accident.
  The converter reproduces it and warns; whether the page meant something is a content question.
- **Anything the warnings name.** A warning is the converter saying it dropped something. Read them
  all before deleting the roff, because after that there is nothing to compare against.

## How it is put together

| file | |
|---|---|
| `man2umm.h` | the document model and the six entry points — the contract |
| `model.cpp` | diagnostics |
| `escape.cpp` | roff escapes, and the `name(N)` cross-reference rule |
| `roff.cpp` | the `-man` reader |
| `emit.cpp` | the dialect writer, the font stream, the model dump |
| `ummread.cpp` | the dialect **reader** |
| `lint.cpp` | canonical shape |
| `run.cpp` | the driver, in the library so `test/` can call it |

**The tool emits the streams the oracle compares** — `-t` the words, `-s` the headings, `-f` the
font of every character. [`scripts/mancheck.py`](../../scripts/mancheck.py) renders the roff with
`groff` and compares; it never parses Markdown. That division is deliberate and was learned the
hard way: when the checker had its own copy of the quote rule, the two disagreed, and a greedy
regex ran from an opening backquote past the real close to the apostrophe in a possessive.

**`ummread.cpp` is the half that outlives the conversion.** The roff reader has a finite job; the
dialect reader is what anything displaying a page goes through, and the renderer of
[`../TODO.md`](../TODO.md) task C25 links this same library and adds a back end. That is why the
library is called `umm` and not `man2umm`, and why the highest-value test in the suite is

```
ummread(write_umm(read_man(x))) == read_man(x)
```

— the guarantee that the format the converter writes is a format something else can read back.

## Three things in here are subtler than they look

**The frame stack (`roff.cpp`).** `.RS`, `.TP` and a tagged `.IP` open a container whose contents
are built in a frame of their own and *moved* into the parent when it closes. Appending to a
`child` vector through a pointer would work until the parent vector reallocated under it. A frame
is *sticky* when only an `.RE` or a heading may close it — that is what tells an `.RS` display,
which survives a `.PP`, from a `.TP` body, which does not.

**A space at a font boundary is roman (`escape.cpp`).** This is the model's canonical form, not a
detail. The writer moves whitespace outside a run's delimiters, `** bold**` being bold in no
Markdown at all — so a space given to either neighbour comes back roman when the dialect is read
again, and the round trip would not close.

**Rule 2 is a test over the whole run (`emit.cpp`).** In `2**41-1` the asterisk run has a digit
before it and a digit after it, so it can neither open nor close and needs no escape. Testing one
asterisk at a time sees the *other asterisk* as its non-alphanumeric flank and escapes both.

## What it does not do

There is **no macro facility in the format and there must not be one**. `.ds` and `.de` are
expanded here and discarded; [`../../lib/libc/man/intro.2.umm`](../../lib/libc/man/intro.2.umm)'s `.de en`
was the corpus's only user macro. A page that wants a repeated phrase types it.

It is **not a Markdown implementation**. `ummread.cpp` parses the dialect and only the dialect: no
link syntax, no HTML, no setext heading, no thematic break, and an unrecognized construct is text
rather than an extension.
