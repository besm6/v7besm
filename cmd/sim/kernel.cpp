//
// The imitation kernel.  kernel.h is the account of why this exists and what it promises;
// this file is the layout, the values and the two device nodes.
//
#include "kernel.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <ctime>

//
// The one line a dmesg has to print.  It is the only value in this file with no counterpart
// in the simulator -- b6sim keeps no kernel diagnostics of its own, so msgbuf would otherwise
// be 128 zero bytes and `dmesg' would print nothing at all, which is indistinguishable from a
// dmesg that is broken.  A fixed string makes the empty case tellable from the working one
// and gives the b6sim half of that program a checked-in .expected.
//
static const char BANNER[] = "BESM-6 Unix (b6sim)\n";

//
// Lay one variable into the image and add its row to the table.
//
// `next' walks upward from the base; every variable therefore lands below KEND, which
// params.cpp asserts is enough room for all of them.  The addresses are b6sim's own and are
// nothing like the real kernel's -- nothing may depend on a particular one, and lib/test/kctlt
// checks only that they are inside the kernel and that they agree with each other.
//
unsigned SimKernel::place(const char *name, unsigned words, unsigned bytes, unsigned &next)
{
    unsigned addr = next;
    next += words;
    table.push_back({ name, addr, bytes });
    return addr;
}

void SimKernel::store_bytes(unsigned addr, const char *s, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        unsigned w     = addr + i / kparam::NBPW;
        unsigned shift = 40 - (i % kparam::NBPW) * 8;
        image[w]       = (image[w] & ~(0xffull << shift)) | ((Word)(uint8_t)s[i] << shift);
    }
}

// Short aliases rather than `using namespace': klayout::P_PID collides with the P_PID of
// the host's <sys/wait.h>, and a `using' would make every mention of it ambiguous.
namespace kp = kparam;
namespace kl = klayout;

//
// Build the image.  The order of the rows is kernel/ksym.c's, so that a KCTL_LIST here and a
// KCTL_LIST there return the names in the same sequence -- not required by anything, but a
// diff between the two harnesses is easier to read when they agree.
//
SimKernel::SimKernel() : image(kp::KREACH, 0)
{
    // Start well above the interrupt vectors, as the real image's data does.  Everything
    // between here and KEND is ours.
    unsigned next = 010000;

    proc_addr = place("proc", kp::NPROC * kl::P_WORDS, kp::NPROC * kl::P_WORDS * kp::NBPW, next);
    place("text", kp::NTEXT * kl::TEXT_WORDS, kp::NTEXT * kl::TEXT_WORDS * kp::NBPW, next);
    place("inode", kp::NINODE * kl::INODE_WORDS, kp::NINODE * kl::INODE_WORDS * kp::NBPW, next);
    place("file", kp::NFILE * kl::FILE_WORDS, kp::NFILE * kl::FILE_WORDS * kp::NBPW, next);
    place("mount", kp::NMOUNT * kl::MOUNT_WORDS, kp::NMOUNT * kl::MOUNT_WORDS * kp::NBPW, next);
    unsigned a_sc = place("sc", kp::NSC * kl::TTY_WORDS, kp::NSC * kl::TTY_WORDS * kp::NBPW, next);
    unsigned a_uhome = place("uhome", 1, kp::NBPW, next);
    a_lbolt          = place("lbolt", 1, kp::NBPW, next);
    a_time           = place("time", 1, kp::NBPW, next);

    // MSGBUFS is a byte count and is not a whole number of words; the buffer still occupies
    // the words it spans, so round up.  The row's size stays the byte count, as the kernel's
    // `sizeof msgbuf' does.
    msgbuf_addr = place("msgbuf", (kp::MSGBUFS + kp::NBPW - 1) / kp::NBPW, kp::MSGBUFS, next);
    unsigned a_msgbufp = place("msgbufp", 1, kp::NBPW, next);

    place("coremap", kp::CMAPSIZ * kl::MAP_WORDS, kp::CMAPSIZ * kl::MAP_WORDS * kp::NBPW, next);
    place("swapmap", kp::SMAPSIZ * kl::MAP_WORDS, kp::SMAPSIZ * kl::MAP_WORDS * kp::NBPW, next);
    unsigned a_nswap   = place("nswap", 1, kp::NBPW, next);
    unsigned a_swplo   = place("swplo", 1, kp::NBPW, next);
    unsigned a_swapdev = place("swapdev", 1, kp::NBPW, next);

    a_dktime = place("dk_time", kl::DKTIME_N, kl::DKTIME_N * kp::NBPW, next);
    a_tknin  = place("tk_nin", 1, kp::NBPW, next);
    a_tknout = place("tk_nout", 1, kp::NBPW, next);

    //
    // The one process.  Its numbers are the ones SYS_getpid, SYS_getuid and SYS_getgid
    // already answer with, so a guest can check the table against something it learned
    // another way -- which is precisely what lib/test/kctlt does, and the reason these are
    // taken from the host rather than made up.
    //
    // Every other slot stays zero, p_stat included, so a scan for a pid cannot match a
    // ghost left over from nothing.
    //
    store(proc_addr + kl::P_CHARS, ((Word)kp::SRUN << 40) | ((Word)kp::SLOAD << 32));
    store(proc_addr + kl::P_UID, (Word)getuid() & BITS41);
    store(proc_addr + kl::P_PGRP, (Word)getpgrp() & BITS41);
    store(proc_addr + kl::P_PID, (Word)getpid() & BITS41);
    // MASKED TO 15 BITS, and that is not tidying.  A guest learns its parent's pid from
    // getppid(), which reads r12 -- an INDEX register, so sys_ok2() has already truncated
    // it there (the ABI limit getpid.2 records).  Storing the host's full number here would
    // disagree with the only value the guest can see, on any host whose pids have got past
    // 32767 -- which is most of them, most of the time.  lib/test/kctlt caught this.
    store(proc_addr + kl::P_PPID, (Word)(getppid() & 077777));
    // Where the image would be, if there were one: the first word an unmapped access
    // cannot name, which is where a real process image starts.
    store(proc_addr + kl::P_ADDR, kp::KREACH);
    store(proc_addr + kl::P_SIZE, MEMORY_NWORDS);

    //
    // The u-area at its fixed address.  A guest reads it with no lookup at all -- that is
    // the whole point of UBASE being a header constant (lib/test/memt.c) -- so it has to be
    // here whether or not anything asked kctl for it.
    //
    store(kp::UBASE + kl::U_UID, (Word)geteuid() & BITS41);
    store(kp::UBASE + kl::U_GID, (Word)getegid() & BITS41);
    store(kp::UBASE + kl::U_RUID, (Word)getuid() & BITS41);
    store(kp::UBASE + kl::U_RGID, (Word)getgid() & BITS41);
    store(kp::UBASE + kl::U_PROCP, proc_addr);
    store(kp::UBASE + kl::U_TTYP, a_sc);
    store(kp::UBASE + kl::U_START, (Word)::time(nullptr) & BITS41);

    // uhome is the p_addr whose u-area is live at UBASE.  With one process it is always
    // this one's, which is also why nothing here ever needs /dev/mem to reach a u-area.
    store(a_uhome, kp::KREACH);

    // The paging store, with conf.c's numbers.  There is none here, and saying so in the
    // kernel's own terms is more use to a pstat than an invented size would be.
    store(a_nswap, 1024);
    store(a_swplo, 0);
    store(a_swapdev, (1 << 8) | 0); // makedev(1, 0) -- the drums

    // The message ring: the banner, and a FAT pointer just past it.  A char * on this
    // machine carries a byte offset in bits 47-45 (offset field 5 is byte #0), and dmesg
    // will ptrword() it -- see <sys/kctl.h>.
    unsigned n = (unsigned)strlen(BANNER);
    store_bytes(msgbuf_addr, BANNER, n);
    unsigned w   = msgbuf_addr + n / kp::NBPW;
    unsigned off = 5 - (n % kp::NBPW);
    store(a_msgbufp, BIT48 | ((Word)off << 44) | w);

    // inode, file, text, mount, coremap, swapmap and sc are left as they were built: zero.
    // b6sim has no counterpart for any of them and will not invent one.
}

//
// The guest's name, for u_comm.  DIRSIZ is 18 and the field is a char array, so a longer
// name is truncated exactly as the kernel's own copy in exec() truncates it.
//
void SimKernel::set_comm(const std::string &name)
{
    std::string base = name;
    size_t slash     = base.find_last_of('/');
    if (slash != std::string::npos)
        base = base.substr(slash + 1);

    char comm[18] = {};
    strncpy(comm, base.c_str(), sizeof(comm) - 1);
    store_bytes(kparam::UBASE + klayout::U_COMM, comm, sizeof(comm));
}

const SimKernel::Entry *SimKernel::find(const std::string &name) const
{
    auto it = std::find_if(table.begin(), table.end(),
                           [&name](const Entry &e) { return name == e.name; });
    return (it == table.end()) ? nullptr : &*it;
}

//
// The live half.  Called before every answer, so that two reads a moment apart differ the
// way they would on a running machine.
//
void SimKernel::refresh(uint64_t instructions)
{
    store(a_time, (Word)::time(nullptr) & BITS41);

    // Ticks, from the instructions actually executed.  The rate is arbitrary -- a BESM-6
    // instruction has no fixed duration and b6sim does not model one -- but it is monotonic
    // and it is derived from something real rather than from the host clock, which `time'
    // already reports.
    uint64_t ticks = instructions / 1000;
    store(a_lbolt, (Word)(ticks % kp::HZ));

    // ALL OF IT IS USER TIME, and that is a fact about this simulator rather than a gap in
    // it: there is no kernel here to bill system time to, no scheduler to be idle, and no
    // nice.  So dk_time[0] moves and [8], [16] and [24] stay zero -- which is what
    // kernel/clock.c would record for a machine that never left user mode.
    store(a_dktime + 0, (Word)ticks & BITS41);

    store(a_tknin, (Word)tk_nin & BITS41);
    store(a_tknout, (Word)tk_nout & BITS41);
}

//
// Copy out of the image.  `byte_off' is the offset within the first word, so a transfer that
// does not start on a word boundary is expressible -- /dev/kmem takes a byte offset like any
// other file (kernel/dev/mem.c).
//
void SimKernel::read_bytes(unsigned addr, unsigned byte_off, char *dst, unsigned n) const
{
    for (unsigned i = 0; i < n; i++) {
        unsigned k = byte_off + i;
        unsigned w = addr + k / kparam::NBPW;
        // Above KREACH is the page pool.  b6sim models no process images there, so it
        // reads as zero rather than as an error: /dev/mem is that long, and a short read
        // would claim an end of file the device does not have.
        Word word      = (w < image.size()) ? image[w] : 0;
        unsigned shift = 40 - (k % kparam::NBPW) * 8;
        dst[i]         = (char)((word >> shift) & 0xff);
    }
}

int SimKernel::dev_minor(const std::string &path)
{
    if (path == "/dev/kmem")
        return 1;
    if (path == "/dev/mem")
        return 0;
    return -1;
}

SimKernel::DevFd *SimKernel::dev_find(int fd)
{
    auto it = devs.find(fd);
    return (it == devs.end()) ? nullptr : &it->second;
}

void SimKernel::dev_dup(int oldfd, int newfd)
{
    auto it = devs.find(oldfd);
    if (it != devs.end())
        devs[newfd] = it->second;
}

//
// How far each node reaches, in words.  /dev/kmem stops where an unmapped access does;
// /dev/mem covers all of core, which for b6sim is the same low image and zeros above it.
//
unsigned SimKernel::dev_limit(int minor)
{
    return (minor == 1) ? (unsigned)kparam::KREACH : 512u * 1024u;
}
