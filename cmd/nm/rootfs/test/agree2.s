// Part two of the host-vs-native nm agreement fixture (task C9c).
//
// The archive's second member, and it exists for the archive cases rather than
// for its symbols: nm walking a library seeks by the member header rather than by
// the file header, prints the member name before each table, and does it all
// again for the next member -- so a one-member archive would leave the loop in
// nm_run() untested.  It defines what agree1.s leaves undefined and vice versa.

        .globl  zsecond, zdatum

        .text
zsecond:
     13 vjm     gtext                   // undefined here: agree1.s has it
        stop    012

        .data
zdatum:
        .word   0
zlocal:
        .word   zsecond
