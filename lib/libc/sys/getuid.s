//
// getuid(), geteuid() -- the real user id, and the effective one.
//
// One extracode, two entry points, exactly as getpid.s: getuid() in kernel/sys4.c sets
// r_val1 to u_ruid and r_val2 to u_uid, and r_val2 arrives in r12.  Neither call can
// fail, so no `14 v1m cerror'; neither takes an argument, so the gate pops nothing.
//
// The x86 port dropped geteuid (lib/tmp/libc/sys/getuid.s has only getuid); v7's own
// PDP-11 libc defined both here, and so does this.
//
        .text
        .globl  getuid, geteuid

getuid:
        $77 24                  // SYS_getuid
     13 uj

geteuid:
        $77 24                  // SYS_getuid
        ita     12              // A := r_val2, the effective uid
     13 uj
