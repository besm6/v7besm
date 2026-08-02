//
// kgetsym(name) -- the kernel word address of an exported variable, or -1.
//
// Not v7's, and not any Unix's: this port has no /unix on the root filesystem, so nlist(3)
// has nothing to open and is deliberately not coming (lib/libc/README.md).  What replaced it
// is kctl(2) over a table the kernel carries (kernel/ksym.c, <sys/kctl.h>).
//
// This is the shorthand for the one thing a caller usually wants.  ps and pstat cannot work
// from values alone -- they resolve p_textp into an index in text[] and u_ttyp into one in
// sc[], which is arithmetic against a base address -- so they ask for the address and read on
// through /dev/kmem, and through /dev/mem for a u-area at p_addr.  lib/test/memt.c is the
// pattern, and the units are its units: a WORD address, so an lseek is `addr * NBPW'.
//
// A tool that wants the value instead should call kctl(name, KCTL_GET, ...) and not this:
// dmesg and iostat read nothing but values and therefore open no device and need no
// privilege at all.
//
// -1 for every failure, errno as kctl(2) set it -- there is nothing this routine can add,
// and a short read of a fixed-size structure is a kernel that disagrees with this header
// about struct kctlstat, which is not a runtime condition.
//
#include <sys/kctl.h>

int kgetsym(const char *name)
{
    struct kctlstat st;

    if (kctl(name, KCTL_STAT, &st, sizeof st) != sizeof st)
        return -1;
    return st.kc_addr;
}
