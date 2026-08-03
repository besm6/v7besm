// Part one of the host-vs-native lorder agreement fixture (task C9c).
//
// lorder prints one "user provider" pair per edge, for tsort(1) to order.  What
// makes an edge is a symbol DEFINED in one object and referenced from another, so
// the three parts of this fixture are wired in a chain -- dep1 uses dep2, dep2
// uses dep3, dep3 uses nothing -- plus the self-edge every .o gets so that an
// object with no edges at all still reaches tsort's output.
//
// The names are one per object and distinct, because the pairs are SORTED and
// joined by name: a tie would come out in whichever order sort(1) chose, and the
// two sorts here are not the same program.

        .globl  one

        .text
one:
     13 vjm     two                     // the edge: dep2.s defines this
        xta     three                   // ... and dep3.s this
        stop    012
