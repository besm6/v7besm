Tasks to do in assembler:

 * AS-1: The operator-bearing mnemonics (a+x, a-x, x-a, a/x, a*x, e+x, e-x, e+n, e-n, j+m) cannot be matched by the current name lexer (+ - * / are not name characters in cmd/as/lex.c). Makeg the lexer to accept them. 

 * AS-2: The 15-bit long-address masks are fixed in pass1.c makecmd() and pass2.c adjust(), and the raw-opcode escapes (LSCMD $NN, LLCMD @NN) now emit the BESM-6 bit positions. STILL TODO: the matching long mask in cmd/ld/ld.c relhalf() is still 20-bit (03777777) and must become 15-bit (077777); deferred with the rest of ld.

 * AS-3: Review the TLIT/TINT/TCOMP/MAKECOMP machinery (the literal-pool `#` path and the 0x4000000 literal bit in pass1.c makecmd(), plus the MAKECOMP macro and component-instruction handling). Tag BESM-6 instruction with these flags where appropriate. Remove dead flags.

 * AS-6: Word-length migration is nearly done. cmd/as + cross/besm6/b.out.h (HDRSZ) + cmd/libaout (fputh/fgeth/fgetsym) and now cmd/ld/{ld.c,size.c,ranlib.c} use the BESM-6 48-bit word (W=6, 3-byte half-words, HDRSZ=54). STILL TODO: cmd/disasm/dis.c still `#define W 8`, so it reads files inconsistent with what `as`/`ld` now emit. Migrate it to W=6 (and any hard-coded 8/72 byte offsets) before it can interoperate.

 * AS-10: copyfil() in cmd/ld/ar.c pads archive member *data* to an even byte count (the IODD/OODD flags, a PDP-11 2-byte-word legacy) instead of to a 6-byte BESM-6 word. The header is now fully word-aligned (ar_name[30], ARHDRSZ=60) and ld advances by `archdr.ar_size + ARHDRSZ`, so member data must also be padded to a whole 6-byte word for member offsets to stay word-aligned. Replace the even-byte padding in copyfil() (and the matching size accounting) with rounding to a multiple of W=6.
