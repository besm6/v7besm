// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// The unctrl codes: one printable rendering per character, indexed 0..0177.
//
// Entry 036 is "^^", where v7's table had "^~".  Count the rows -- twelve, twelve, eight --
// and the sixth entry of the third row lands on 036, which is `^' xor 0100, not `~'.  A
// transcription slip that no v7 program happened to print.
//
// The subscript is masked by unctrl() in <unctrl.h>, which is where the macro lives; see
// that file for why the mask is not optional on this machine.
//

// clang-format off
char *_unctrl[] = {
    "^@", "^A", "^B", "^C", "^D", "^E", "^F", "^G", "^H", "^I", "^J", "^K",
    "^L", "^M", "^N", "^O", "^P", "^Q", "^R", "^S", "^T", "^U", "^V", "^W",
    "^X", "^Y", "^Z", "^[", "^\\", "^]", "^^", "^_",
    " ", "!", "\"", "#", "$",  "%", "&", "'", "(", ")", "*", "+", ",", "-",
    ".", "/", "0",  "1", "2",  "3", "4", "5", "6", "7", "8", "9", ":", ";",
    "<", "=", ">",  "?", "@",  "A", "B", "C", "D", "E", "F", "G", "H", "I",
    "J", "K", "L",  "M", "N",  "O", "P", "Q", "R", "S", "T", "U", "V", "W",
    "X", "Y", "Z",  "[", "\\", "]", "^", "_", "`", "a", "b", "c", "d", "e",
    "f", "g", "h",  "i", "j",  "k", "l", "m", "n", "o", "p", "q", "r", "s",
    "t", "u", "v",  "w", "x",  "y", "z", "{", "|", "}", "~", "^?"
};
// clang-format on
