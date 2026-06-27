Tasks to do in assembler:

 * AS-1: The operator-bearing mnemonics (a+x, a-x, x-a, a/x, a*x, e+x, e-x, e+n, e-n, j+m) cannot be matched by the current name lexer (+ - * / are not name characters in cmd/as/lex.c). They are stored verbatim per the chosen mnemonics; making the lexer accept them (e.g. the comma-delimited ,a+x, Madlen syntax shown in doc/Besm6_Calling_Conventions.md) is a separate task.

 * AS-2: The 15-bit long-address masks are fixed in pass1.c makecmd() and pass2.c adjust(), and the raw-opcode escapes (LSCMD $NN, LLCMD @NN) now emit the BESM-6 bit positions. STILL TODO: the matching long mask in cmd/ld/ld.c relhalf() is still 20-bit (03777777) and must become 15-bit (077777); deferred with the rest of ld.

 * AS-3: Removing the dead TLIT/TINT/TCOMP/MAKECOMP machinery (the literal-pool `#` path and the 0x4000000 literal bit in pass1.c makecmd(), plus the MAKECOMP macro and component-instruction handling). No BESM-6 instruction uses these flags.

 * AS-4: Command-line option parsing is broken in as.c main(): the inner loop `for (cp = argv[i]; ...)` starts at the leading '-', so every flag (including -o) falls into the "Unknown option" default. Start the scan at argv[i]+1.

 * AS-5: makeheader() in pass2.c does not initialize hdr.a_abss, so the abss size field is written as garbage. Set it (currently always 0) along with the other a_* fields.

 * AS-6: Word-length migration is only half done. cmd/as + cross/besm6/b.out.h (HDRSZ) + cmd/libaout (fputh/fgeth/fgetsym) now use the BESM-6 48-bit word (W=6, 3-byte half-words, HDRSZ=54). But cmd/ld/ld.c, cmd/disasm/dis.c, cmd/ld/size.c and cmd/ld/ranlib.c still `#define W 8`, so they read/produce files inconsistent with what `as` now emits. Migrate each to W=6 (and any hard-coded 8/72 byte offsets) before they can interoperate with as's output.

 * AS-7: The archive int codec cmd/libaout/{putint,getint}.c still encodes an int as 2 value bytes + 6 zero pad (an 8-byte "Elbrus-B" word). If the archive word is meant to follow the object word, migrate it to 3 value + 3 pad (6 bytes); deferred with ranlib.

 * AS-8: Five cmd/libaout sources (fputran.c, getarhdr.c, getint.c, putarhdr.c, putint.c) are still un-ported K&R: they use implicit-int definitions and old `ranlib.h`-style includes instead of the besm6/ cross headers, so they fail under -Werror and are left out of cmd/libaout/CMakeLists.txt. Port them to ANSI prototypes + besm6/ headers (matching their fget*/fput* siblings) and add them back to the library.

 * AS-9: The helper-declaration comments in cross/besm6/b.out.h are stale. They state that fgeth/fputh "read and write one 4-byte word" and that the helpers are "implemented in cmd/ld/", but the unit is actually a 3-byte half-word (24 bits) and the implementations now live in cmd/libaout. Fix the comment block above the fgeth/fputh/fgethdr/fgetsym/fgetint declarations (and the getint/putint note) to say 3-byte half-word and cmd/libaout.
