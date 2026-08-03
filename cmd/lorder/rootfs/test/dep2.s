// Part two of the host-vs-native lorder agreement fixture (task C9c).
// Defines what dep1.s calls and calls what dep3.s defines: the middle of the chain.

        .globl  two

        .text
two:
     13 vjm     three
        stop    012
