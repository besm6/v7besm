/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The error messages, indexed by errno.
 *
 * Exactly 35 entries, 0..34, and no more is possible: the value the $77 gate leaves in
 * r14 is always one of the numbers include/errno.h defines.  The kernel assigns from
 * its own copy of the list (include/sys/user.h) and b6sim folds the host's numbering
 * onto the same 34 (guest_errno() in cmd/sim/syscall.cpp), so nothing else can arrive.
 * 33 and 34 -- EDOM and ERANGE -- are math software rather than kernel codes; they are
 * here because v7 put them here, and libm will be the only thing that sets them.
 *
 * An array of `char *' is one word per element: a fat pointer fits in a word, marker
 * bit and byte offset and all (doc/Besm6_Data_Representation.md).
 */
char *sys_errlist[] = {
    "Error 0",
    "Not owner",
    "No such file or directory",
    "No such process",
    "Interrupted system call",
    "I/O error",
    "No such device or address",
    "Arg list too long",
    "Exec format error",
    "Bad file number",
    "No children",
    "No more processes",
    "Not enough core",
    "Permission denied",
    "Bad address",
    "Block device required",
    "Mount device busy",
    "File exists",
    "Cross-device link",
    "No such device",
    "Not a directory",
    "Is a directory",
    "Invalid argument",
    "File table overflow",
    "Too many open files",
    "Not a typewriter",
    "Text file busy",
    "File too large",
    "No space left on device",
    "Illegal seek",
    "Read-only file system",
    "Too many links",
    "Broken pipe",
    "Argument too large",
    "Result too large",
};

int sys_nerr = sizeof sys_errlist / sizeof sys_errlist[0];
