//
// Unix v7 system calls for the user-level BESM-6 simulator.
//
// Each guest syscall (issued as `$77 N`, see extracode.cpp) is mapped onto the
// host operating system, in the spirit of Warren Toomey's `apout` for the
// PDP-11 (reference: cmd/sim/tmp/apout/v7trap.c).  The syscall numbers come from
// kernel/sysent.c.
//
// BESM-6 calling convention (doc/Besm6_Calling_Conventions.md): for a call of N
// arguments the last one is in the accumulator and arguments 1..N-1 sit just
// below the stack pointer M[017].  The result is returned in the accumulator and
// errno (0 on success) in register M[14].  The five calls v7 gives a SECOND
// result -- pipe, wait, getpid, getuid, getgid -- return it in r12 (sys_ok2),
// which is what the kernel does too (R_VAL2 in include/sys/reg.h), so one binary
// runs on both.  r13 is left alone: it is the caller's return address.
//
// BESM-6 makes this much simpler than apout: every C scalar is one 48-bit word,
// so time_t/off_t and every struct stat field are a single word — none of the
// PDP-11 high/low splitting or middle-endian copylong juggling is needed.
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

#include <csignal>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "machine.h"
#include "memory.h"

//
// Unix v7 system call numbers (kernel/sysent.c).
//
// The guest has the same list, by the same names, in include/sys/syscall.h, which is
// what lib/libc/sys/ issues its extracodes through; keep the two in step by hand.  It
// cannot simply be #included here: b6sim is a HOST tool, and putting include/ on its
// -I path would shadow the <stdio.h>, <errno.h>, <sys/stat.h> and <sys/times.h> it
// includes for real.
//
enum {
    SYS_exit   = 1,
    SYS_fork   = 2,
    SYS_read   = 3,
    SYS_write  = 4,
    SYS_open   = 5,
    SYS_close  = 6,
    SYS_wait   = 7,
    SYS_creat  = 8,
    SYS_link   = 9,
    SYS_unlink = 10,
    SYS_exec   = 11,
    SYS_chdir  = 12,
    SYS_time   = 13,
    SYS_mknod  = 14,
    SYS_chmod  = 15,
    SYS_chown  = 16,
    SYS_break  = 17,
    SYS_stat   = 18,
    SYS_seek   = 19,
    SYS_getpid = 20,
    SYS_mount  = 21,
    SYS_umount = 22,
    SYS_setuid = 23,
    SYS_getuid = 24,
    SYS_stime  = 25,
    SYS_ptrace = 26,
    SYS_alarm  = 27,
    SYS_fstat  = 28,
    SYS_pause  = 29,
    SYS_utime  = 30,
    SYS_stty   = 31,
    SYS_gtty   = 32,
    SYS_access = 33,
    SYS_nice   = 34,
    SYS_ftime  = 35,
    SYS_sync   = 36,
    SYS_kill   = 37,
    SYS_dup    = 41,
    SYS_pipe   = 42,
    SYS_times  = 43,
    SYS_profil = 44,
    SYS_sigret = 45,
    SYS_setgid = 46,
    SYS_getgid = 47,
    SYS_signal = 48,
    SYS_acct   = 51,
    SYS_phys   = 52,
    SYS_lock   = 53,
    SYS_ioctl  = 54,
    SYS_exece  = 59,
    SYS_umask  = 60,
    SYS_chroot = 61,
};

//
// Number of C arguments each syscall is called with (v7 prototypes).  Drives
// the callee stack cleanup at the end of Processor::syscall (r15 -= N-1); it
// must match the `count` passed to syscall_arg() in each case below.  The count
// is the true prototype arity even for calls whose arguments b6sim ignores
// (e.g. ioctl, mount): the caller still pushed those words and expects them
// popped.  Pointer arguments passed in the accumulator (times/ftime) count as
// the single, last argument, so those are 1.
//
// wait and pipe take NO arguments here, unlike their v7 C prototypes: both
// deliver their second result in r12 (see sys_ok2) rather than through a user
// buffer, matching the kernel (include/sys/reg.h, R_VAL2).
//
static unsigned syscall_nargs(unsigned num)
{
    switch (num) {
    // Zero-argument calls: leave r15 unchanged.
    case SYS_fork:
    case SYS_time:
    case SYS_getpid:
    case SYS_getuid:
    case SYS_getgid:
    case SYS_pause:
    case SYS_sync:
    case SYS_wait:
    case SYS_pipe:
    // sigret takes none either, and must not: the signal frame sits just below
    // r15 where the handler left it, so a pop here would move the frame out
    // from under the restore (kernel/sendsig.c, kernel/sysent.c row 45).
    case SYS_sigret:
        return 0;

    // Two-argument calls that libc calls with fewer C arguments.  dup(fd) is
    // written dup(fd, 0) and dup2(fd, fd2) as dup(fd | 0100, fd2): v7 hangs
    // dup2 off the same entry by a bit in the first argument, so the kernel's
    // dup() reads a two-field arg struct either way (kernel/sys3.c) and the
    // count here is 2 for both.  libc/sys/dup.s pushes the extra word.
    case SYS_dup:
        return 2;

    // Three-argument calls.
    case SYS_read:
    case SYS_write:
    case SYS_mknod:
    case SYS_chown:
    case SYS_seek:
    case SYS_exece:
    case SYS_ioctl:
    case SYS_mount:
        return 3;

    // Four-argument calls.
    case SYS_ptrace:
    case SYS_profil:
        return 4;

    // Two-argument calls.
    case SYS_open:
    case SYS_creat:
    case SYS_link:
    case SYS_chmod:
    case SYS_stat:
    case SYS_fstat:
    case SYS_utime:
    case SYS_access:
    case SYS_kill:
    case SYS_stty:
    case SYS_gtty:
    case SYS_signal:
    case SYS_exec:
        return 2;

    // Everything else takes one argument (single stack/ACC operand).
    default:
        return 1;
    }
}

//
// Map a host errno to the guest's value (include/errno.h).  The classic Unix
// codes 1..34 are identical on the guest and on every host we build on, so the
// mapping is mostly the identity; the switch pins the ones that can differ
// (e.g. EAGAIN on macOS).
//
// Nothing outside 1..34 may reach the guest: v7 has no number for it, and libc's
// sys_errlist will have 35 entries, so a host-only code would come out of perror
// as "Unknown error".  The conditions a v7 syscall can plausibly provoke on a
// modern host are folded onto the nearest v7 code and everything else onto EIO.
//
static int guest_errno(int e)
{
    switch (e) {
    case EPERM:   return 1;
    case ENOENT:  return 2;
    case ESRCH:   return 3;
    case EINTR:   return 4;
    case EIO:     return 5;
    case ENXIO:   return 6;
    case E2BIG:   return 7;
    case ENOEXEC: return 8;
    case EBADF:   return 9;
    case ECHILD:  return 10;
    case EAGAIN:  return 11;
    case ENOMEM:  return 12;
    case EACCES:  return 13;
    case EFAULT:  return 14;
    case ENOTBLK: return 15;
    case EBUSY:   return 16;
    case EEXIST:  return 17;
    case EXDEV:   return 18;
    case ENODEV:  return 19;
    case ENOTDIR: return 20;
    case EISDIR:  return 21;
    case EINVAL:  return 22;
    case ENFILE:  return 23;
    case EMFILE:  return 24;
    case ENOTTY:  return 25;
    case ETXTBSY: return 26;
    case EFBIG:   return 27;
    case ENOSPC:  return 28;
    case ESPIPE:  return 29;
    case EROFS:   return 30;
    case EMLINK:  return 31;
    case EPIPE:   return 32;
    case EDOM:    return 33;
    case ERANGE:  return 34;

    // A path a v7 system could never have named at all.
    case ENAMETOOLONG:
    case ELOOP: return 2; // ENOENT

    // Asked of the host something it declines to do; v7's answer is EINVAL.
    // ENOTSUP and EOPNOTSUPP are one value on Linux and two on macOS, so the
    // second label has to be conditional or the switch would not compile.
#if defined(ENOTSUP) && ENOTSUP != EOPNOTSUPP
    case ENOTSUP:
#endif
    case EOPNOTSUPP:
    case ENOSYS: return 22; // EINVAL

    // Everything else: a host condition v7 has no number for.
    default: return 5; // EIO
    }
}

//
// Sign-extend a guest int (41-bit two's complement in bits 41..1).
//
static int64_t sign_extend41(Word w)
{
    int64_t v = w & BITS41;
    if (w & BIT41)
        v -= (int64_t)1 << 41;
    return v;
}

//
// Turn a char*/void* fat pointer into a BytePointer.  Fat pointer layout
// (doc/Besm6_Data_Representation.md): bit 48 set, byte-offset field in bits
// 47..45 (field 5 = byte #0/MSB), word address in bits 15..1.
//
static BytePointer fat_to_byteptr(Memory &mem, Word fatptr)
{
    unsigned word_addr = fatptr & BITS(15);
    unsigned offset    = (fatptr >> 44) & 7;
    unsigned byte_idx  = (offset <= 5) ? 5 - offset : 0;
    return BytePointer(mem, word_addr, byte_idx);
}

//
// Store a host struct stat into the guest's 11-word struct stat
// (include/sys/stat.h): one word per field, no packing.
//
static void store_stat(Machine &m, unsigned addr, const struct stat &st)
{
    m.mem_store(addr + 0, (Word)st.st_dev & BITS41);
    m.mem_store(addr + 1, (Word)st.st_ino & BITS41);
    m.mem_store(addr + 2, (Word)st.st_mode & BITS41);
    m.mem_store(addr + 3, (Word)st.st_nlink & BITS41);
    m.mem_store(addr + 4, (Word)st.st_uid & BITS41);
    m.mem_store(addr + 5, (Word)st.st_gid & BITS41);
    m.mem_store(addr + 6, (Word)st.st_rdev & BITS41);
    m.mem_store(addr + 7, (Word)st.st_size & BITS41);
    m.mem_store(addr + 8, (Word)st.st_atime & BITS41);
    m.mem_store(addr + 9, (Word)st.st_mtime & BITS41);
    m.mem_store(addr + 10, (Word)st.st_ctime & BITS41);
}

//
// Fetch the k-th argument (1-based) of a call passing `count` arguments.
//
Word Processor::syscall_arg(unsigned k, unsigned count)
{
    if (k >= count)
        return core.ACC & BITS48;
    return machine.mem_load(core.M[017] - (count - k));
}

//
// Read a NUL-terminated string addressed by a char* fat pointer.
//
std::string Processor::mem_get_string(Word fatptr)
{
    BytePointer bp = fat_to_byteptr(memory, fatptr);
    std::string s;
    for (unsigned i = 0; i < MEMORY_NWORDS * 6; i++) {
        uint8_t c = bp.get_byte();
        if (c == 0)
            break;
        s.push_back((char)c);
    }
    return s;
}

//
// Copy `n` bytes from a char* fat pointer into a host buffer.
//
void Processor::mem_get_bytes(Word fatptr, char *dst, unsigned n)
{
    BytePointer bp = fat_to_byteptr(memory, fatptr);
    for (unsigned i = 0; i < n; i++)
        dst[i] = (char)bp.get_byte();
}

//
// Copy `n` bytes from a host buffer into a char* fat pointer.
//
void Processor::mem_put_bytes(Word fatptr, const char *src, unsigned n)
{
    BytePointer bp = fat_to_byteptr(memory, fatptr);
    for (unsigned i = 0; i < n; i++)
        bp.put_byte((uint8_t)src[i]);
}

//
// Finish a syscall on success: result in the accumulator, errno cleared.
// The result is stored as a guest int (41 bits, sign in bit 41).
//
void Processor::sys_ok(int64_t result)
{
    core.ACC   = (Word)result & BITS41;
    core.M[14] = 0;
}

//
// Finish a syscall that has TWO results: the first in the accumulator as usual,
// the second in r12, errno cleared.  This is v7's second-return-value convention
// (the PDP-11 put it in r1), used by pipe, wait, getpid, getuid and getgid.
//
// r12 and not r13: r13 is the ABI's return-address register and belongs to the
// caller (doc/Besm6_Calling_Conventions.md).  It matches the kernel exactly --
// R_VAL2 in include/sys/reg.h -- so one binary runs on both.
//
// NOTE THE WIDTH.  An index register is 15 bits, so a second result above 32767
// is truncated.  Pids stop below 30000 and fds/uids/gids are far smaller, but a
// HOST pid can overflow -- so getpid()'s and fork()'s second value may not match
// the host's getppid() when b6sim runs on a modern system -- and so can a WAIT
// STATUS: it is (code << 8), which passes 32767 as soon as the exit code passes
// 127.  Exit codes 128..255 therefore come out of wait() wrong, identically on
// the kernel and here (u.u_r.r_val2 lands in r12 the same way).  Widening it
// means giving wait() an argument and writing the status through the caller's
// pointer, which is a gate change; it is recorded in lib/README.md instead.
// The first result is unaffected: it goes to the accumulator, a full word.
//
void Processor::sys_ok2(int64_t v1, int64_t v2)
{
    core.ACC   = (Word)v1 & BITS41;
    core.M[12] = ADDR(v2);
    core.M[14] = 0;
}

//
// Finish a syscall on error: -1 in the accumulator, errno in M[14].
//
void Processor::sys_err(int host_errno)
{
    core.ACC   = (Word)(-1) & BITS41;
    core.M[14] = guest_errno(host_errno);
}

//
// Common tail: -1 means the host call failed and set errno.
//
void Processor::sys_ret(int64_t result)
{
    if (result == -1)
        sys_err(errno);
    else
        sys_ok(result);
}

//
// exec()/exece(): replace the process image with a new a.out.
//
void Processor::sys_exec(unsigned count, bool with_env)
{
    std::string path = mem_get_string(syscall_arg(1, count));
    Word argvptr     = syscall_arg(2, count);
    Word envpptr     = with_env ? syscall_arg(3, count) : 0;

    // Walk a guest array of char* fat pointers into a vector of host strings.
    auto read_vec = [&](Word vec) -> std::vector<std::string> {
        std::vector<std::string> out;
        unsigned addr = vec & BITS(15);
        if (addr == 0)
            return out;
        for (unsigned i = 0; i < 4096; i++) {
            Word p = machine.mem_load(addr + i);
            if (p == 0)
                break;
            out.push_back(mem_get_string(p));
        }
        return out;
    };

    std::vector<std::string> argv = read_vec(argvptr);
    std::vector<std::string> envp = with_env ? read_vec(envpptr) : std::vector<std::string>();

    fflush(nullptr);
    try {
        // On success the image is replaced and execution continues at the new
        // entry point; exec() sets ACC/M[14] for the main(argc, argv) call.
        machine.exec(path, argv, envp);
    } catch (const std::exception &) {
        // Failure returns to the C caller, so clean up its pushed arguments
        // exactly as the common tail in Processor::syscall would.
        sys_err(ENOENT);
        if (count >= 2)
            core.M[017] = ADDR(core.M[017] - (count - 1));
    }
}

//
// ---------------------------------------------------------------------------
// Signal delivery
// ---------------------------------------------------------------------------
//
// The guest runs its own handlers, and the frame it runs them on is the kernel's,
// word for word (kernel/sendsig.c).  b6sim does what the kernel does:
//
//   * signal() records the handler HERE and does not hand it to the host: a guest
//     handler is guest machine code and cannot run in a host signal context.  What
//     the host gets instead is sim_sigcatch(), which only records the arrival.
//   * A pending signal is delivered at the END OF A SERVICED EXTRACODE, which is
//     where the kernel delivers it too -- psig() runs in sysret() (kernel/syscall.c)
//     and clock() does not deliver at all, so a guest spinning in user code gets
//     nothing until it traps.  On both.
//   * Delivery pushes the 21-word reg.h frame, plants a `$77 SYS_sigret' word above
//     it, and enters the handler with the number in the accumulator, r14 = -1 and
//     r13 naming the planted word.  The handler returns into it and lands in
//     sys_sigret() below.
//
// The frame layout is a THIRD hand-maintained copy of include/sys/reg.h, for the same
// reason the syscall numbers are a second copy of <sys/syscall.h>: b6sim is a host
// tool and cannot put include/ on its -I path.  Keep them in step.
//
enum {
    GUEST_NSIG = 17, // <signal.h>: signals 1..16
    NREGFRAME  = 21, // include/sys/reg.h: words in the trap frame

    // Frame word indices.  0 ACC, 1 R, 2 Y, 3 RET, 4 SPSW, 5 M[16], then the
    // general registers DESCENDING: M15 at 6 ... M1 at 20, so M[i] is at 21 - i.
    FRAME_ACC  = 0,
    FRAME_R    = 1,
    FRAME_Y    = 2,
    FRAME_RET  = 3,
    FRAME_SPSW = 4,
    FRAME_MOD  = 5,

    // The two SPSW bits that say how to resume (include/sys/reg.h, SPSW_USER).
    SPSW_MOD_RK      = 00020,
    SPSW_RIGHT_INSTR = 00400,

    // The AU mode word compiled code runs in: `ntr 7', NTR 3 + logical ω
    // (include/sys/reg.h, RREG_C; doc/Besm6_Runtime_Library.md).
    RREG_C = 7,
};

//
// `$77 SYS_sigret' in the LEFT half of a word -- what kernel/besm6.S assembles as
// `sigcode' and sendsig() copies onto the user stack.  Short instruction format:
// bits 24-21 the index register (none), bit 20 clear, bits 18-13 the opcode, bits
// 12-1 the address (doc/Besm6_Instruction_Set.md, "Format 1").  The right half is
// never executed: an extracode returns to the left half of the next word.
//
static const Word SIGCODE_WORD = (Word)(((077u << 12) | SYS_sigret)) << 24;

//
// Guest signal dispositions: 0 = SIG_DFL, 1 = SIG_IGN, anything else a guest word
// address.  Static rather than per-Processor because the host handler below has to
// reach the pending set, and a host signal handler takes no argument but the number.
// One simulated process per b6sim, so there is nothing to disambiguate.
//
static Word guest_handler[GUEST_NSIG];
static volatile std::sig_atomic_t sig_pending[GUEST_NSIG];

//
// The host-side catcher.  It records the arrival and returns, which is also what
// makes a blocking syscall come back with EINTR -- the guest's handler runs from
// check_signals() once the syscall has been serviced.
//
// Guest signal numbers are v7's and are passed to the host unchanged, as everywhere
// else in this file (::kill, ::alarm); they agree for everything a v7 program raises
// on the BSD-derived hosts b6sim is built on.
//
static void sim_sigcatch(int sig)
{
    if (sig > 0 && sig < (int)GUEST_NSIG)
        sig_pending[sig] = 1;
}

//
// Is a signal the guest catches waiting to be delivered?  pause() asks before it
// blocks; see there.
//
static bool signal_pending()
{
    for (unsigned sig = 1; sig < GUEST_NSIG; sig++)
        if (sig_pending[sig] && guest_handler[sig] > 1)
            return true;
    return false;
}

//
// Build the signal frame and enter the handler.  Called with the machine state
// already advanced past the extracode, so what is saved is exactly where the guest
// would have resumed.
//
void Processor::deliver_signal(unsigned sig)
{
    unsigned handler = (unsigned)(guest_handler[sig] & BITS(15));
    unsigned n       = core.M[017]; // first free user word; the stack grows up

    // The interrupted context, in reg.h order.
    machine.mem_store(n + FRAME_ACC, core.ACC);
    machine.mem_store(n + FRAME_R, core.RAU);
    machine.mem_store(n + FRAME_Y, core.RMR);
    machine.mem_store(n + FRAME_RET, core.PC);
    machine.mem_store(n + FRAME_SPSW, (core.right_instr_flag ? SPSW_RIGHT_INSTR : 0) |
                                          (core.apply_mod_reg ? SPSW_MOD_RK : 0));
    machine.mem_store(n + FRAME_MOD, core.MOD);
    for (unsigned i = 1; i <= 15; i++)
        machine.mem_store(n + NREGFRAME - i, core.M[i]);

    // The return path, and the whole of it.
    machine.mem_store(n + NREGFRAME, SIGCODE_WORD);

    // v7 resets a caught signal to SIG_DFL as it delivers it, except for the two
    // the tracer owns (psig(), kernel/sig.c: SIGINS = 4 and SIGTRC = 5).
    if (sig != 4 && sig != 5)
        guest_handler[sig] = 0;

    core.ACC              = sig;               // the handler's one argument
    core.M[14]            = ADDR(-1);          // argument count, negative
    core.M[13]            = n + NREGFRAME;     // return address: the planted word
    core.M[017]           = n + NREGFRAME + 1; // the handler's frame starts above it
    core.MOD              = 0;
    core.apply_mod_reg    = false;
    core.RAU              = RREG_C; // no crt0 in front of a handler
    core.PC               = handler;
    core.right_instr_flag = false;
}

//
// Deliver one pending signal, if any.  One per trap is enough: a second stays
// pending and is delivered at the next one, exactly as p_sig does.
//
void Processor::check_signals()
{
    for (unsigned sig = 1; sig < GUEST_NSIG; sig++) {
        if (!sig_pending[sig])
            continue;
        sig_pending[sig] = 0;
        if (guest_handler[sig] > 1) {
            deliver_signal(sig);
            return;
        }
    }
}

//
// sigret(): the syscall the planted trampoline issues, and the only way out of a
// handler.  It takes no arguments -- the handler's epilogue leaves r15 as delivery
// set it, so the frame is the 21 words below the trampoline word.
//
// Nothing is reported back through the accumulator afterwards, which is why this
// returns instead of calling sys_ok(): ACC, r12 and r14 are three of the registers
// just restored.  The kernel says the same thing with u.u_justreturn.
//
void Processor::sys_sigret()
{
    unsigned n = ADDR(core.M[017] - (NREGFRAME + 1));
    Word spsw  = machine.mem_load(n + FRAME_SPSW);

    core.ACC = machine.mem_load(n + FRAME_ACC);
    core.RAU = (unsigned)machine.mem_load(n + FRAME_R) & BITS(6);
    core.RMR = machine.mem_load(n + FRAME_Y);
    core.MOD = (unsigned)machine.mem_load(n + FRAME_MOD) & BITS(15);
    for (unsigned i = 1; i <= 15; i++)
        core.M[i] = (unsigned)machine.mem_load(n + NREGFRAME - i) & BITS(15);

    core.PC               = (unsigned)machine.mem_load(n + FRAME_RET) & BITS(15);
    core.right_instr_flag = (spsw & SPSW_RIGHT_INSTR) != 0;
    core.apply_mod_reg    = (spsw & SPSW_MOD_RK) != 0;
}

//
// Dispatch a Unix v7 system call.
//
void Processor::syscall(unsigned num)
{
    switch (num) {
    case SYS_exit:
        // void _exit(int status): status is in the accumulator. No return.
        // Pass the raw accumulator: set_exit_status() keeps the low byte as the
        // process return code and prints the full 41-bit value under --status.
        machine.set_exit_status(core.ACC);
        throw Exception(""); // empty message: clean halt

    case SYS_fork: {
        // fork(): v7's two-value return, matching the kernel (kernel/sys1.c).
        // Each side gets the OTHER's pid in the accumulator -- distinct, but not
        // self-identifying -- so r12 says which side you are: 1 in the child,
        // 0 in the parent.  (Host fork's own "0 in the child" is not the guest
        // ABI; do not lean on the accumulator to tell them apart.)
        fflush(nullptr);
        pid_t pid = fork();
        if (pid < 0)
            sys_err(errno);
        else if (pid == 0)
            sys_ok2(::getppid(), 1); // the child
        else
            sys_ok2(pid, 0); // the parent
        break;
    }

    case SYS_read: {
        // int read(int fd, char *buf, int n)
        int fd       = (int)sign_extend41(syscall_arg(1, 3));
        Word bufptr  = syscall_arg(2, 3);
        int64_t n    = sign_extend41(syscall_arg(3, 3));
        if (n < 0 || (uint64_t)n > (uint64_t)MEMORY_NWORDS * 6) {
            sys_err(EINVAL);
            break;
        }
        std::vector<char> buf(n);
        ssize_t r = ::read(fd, buf.data(), n);
        if (r < 0)
            sys_err(errno);
        else {
            mem_put_bytes(bufptr, buf.data(), (unsigned)r);
            sys_ok(r);
        }
        break;
    }

    case SYS_write: {
        // int write(int fd, char *buf, int n)
        int fd      = (int)sign_extend41(syscall_arg(1, 3));
        Word bufptr = syscall_arg(2, 3);
        int64_t n   = sign_extend41(syscall_arg(3, 3));
        if (n < 0 || (uint64_t)n > (uint64_t)MEMORY_NWORDS * 6) {
            sys_err(EINVAL);
            break;
        }
        std::vector<char> buf(n);
        mem_get_bytes(bufptr, buf.data(), (unsigned)n);
        sys_ret(::write(fd, buf.data(), n));
        break;
    }

    case SYS_open: {
        // int open(char *path, int mode): v7 mode 0/1/2 -> RDONLY/WRONLY/RDWR.
        std::string path = mem_get_string(syscall_arg(1, 2));
        int vmode        = (int)sign_extend41(syscall_arg(2, 2));
        int hmode        = (vmode == 0) ? O_RDONLY : (vmode == 1) ? O_WRONLY : O_RDWR;
        sys_ret(::open(path.c_str(), hmode));
        break;
    }

    case SYS_close:
        // int close(int fd)
        sys_ret(::close((int)sign_extend41(syscall_arg(1, 1))));
        break;

    case SYS_wait: {
        // wait(): no arguments.  The pid comes back in the accumulator and the
        // status in r12, rather than through a user buffer -- the kernel's
        // wait() sets u_r.r_val1/r_val2 the same way (kernel/sys1.c).
        int status = 0;
        pid_t pid  = ::wait(&status);
        if (pid < 0) {
            sys_err(errno);
            break;
        }
        // v7 status: high byte = exit code, low byte = terminating signal.
        int v7 = 0;
        if (WIFEXITED(status))
            v7 = (WEXITSTATUS(status) & 0xff) << 8;
        else if (WIFSIGNALED(status))
            v7 = WTERMSIG(status) & 0x7f;
        sys_ok2(pid, v7);
        break;
    }

    case SYS_creat: {
        // int creat(char *path, int mode)
        std::string path = mem_get_string(syscall_arg(1, 2));
        int mode         = (int)sign_extend41(syscall_arg(2, 2));
        sys_ret(::creat(path.c_str(), mode));
        break;
    }

    case SYS_link: {
        // int link(char *name1, char *name2)
        std::string name1 = mem_get_string(syscall_arg(1, 2));
        std::string name2 = mem_get_string(syscall_arg(2, 2));
        sys_ret(::link(name1.c_str(), name2.c_str()));
        break;
    }

    case SYS_unlink:
        // int unlink(char *path)
        sys_ret(::unlink(mem_get_string(syscall_arg(1, 1)).c_str()));
        break;

    case SYS_exec:
        // On success the image is replaced and r15 reseeded, so the tail pop
        // below must not run; sys_exec pops itself on failure.
        sys_exec(2, false);
        return;

    case SYS_exece:
        sys_exec(3, true);
        return;

    case SYS_chdir:
        // int chdir(char *path)
        sys_ret(::chdir(mem_get_string(syscall_arg(1, 1)).c_str()));
        break;

    case SYS_time:
        // time_t time(void): seconds since the epoch in one word.
        sys_ok((int64_t)::time(nullptr));
        break;

    case SYS_mknod: {
        // int mknod(char *path, int mode, int dev)
        std::string path = mem_get_string(syscall_arg(1, 3));
        int mode         = (int)sign_extend41(syscall_arg(2, 3));
        int dev          = (int)sign_extend41(syscall_arg(3, 3));
        if ((mode & S_IFMT) == S_IFDIR)
            sys_ret(::mkdir(path.c_str(), mode & 07777));
        else
            sys_ret(::mknod(path.c_str(), mode, dev));
        break;
    }

    case SYS_chmod: {
        // int chmod(char *path, int mode)
        std::string path = mem_get_string(syscall_arg(1, 2));
        int mode         = (int)sign_extend41(syscall_arg(2, 2));
        sys_ret(::chmod(path.c_str(), mode));
        break;
    }

    case SYS_chown: {
        // int chown(char *path, int uid, int gid)
        std::string path = mem_get_string(syscall_arg(1, 3));
        int uid          = (int)sign_extend41(syscall_arg(2, 3));
        int gid          = (int)sign_extend41(syscall_arg(3, 3));
        sys_ret(::chown(path.c_str(), uid, gid));
        break;
    }

    case SYS_break: {
        // int break(char *addr): set the program break (word address), rounded up
        // to a page boundary.  The heap may not grow into the stack, so fail if the
        // page-aligned break reaches STACK_BASE -- the kernel's own ceiling
        // (estabur()'s `nt + nd > USTKPAGE * PGSZ').  Not M[017]: that starts ABOVE
        // the argument block exec() lays at STACK_BASE, and climbs as the program
        // runs, so it would let the heap eat the arguments.
        unsigned addr = (unsigned)(syscall_arg(1, 1) & BITS(15));
        addr          = (addr + PAGE_NWORDS - 1) / PAGE_NWORDS * PAGE_NWORDS;
        if (addr >= STACK_BASE)
            sys_err(ENOMEM);
        else {
            machine.set_program_break(addr);
            sys_ok(0);
        }
        break;
    }

    case SYS_stat: {
        // int stat(char *path, struct stat *buf)
        std::string path = mem_get_string(syscall_arg(1, 2));
        unsigned bufaddr = syscall_arg(2, 2) & BITS(15);
        struct stat st;
        if (::stat(path.c_str(), &st) < 0)
            sys_err(errno);
        else {
            store_stat(machine, bufaddr, st);
            sys_ok(0);
        }
        break;
    }

    case SYS_fstat: {
        // int fstat(int fd, struct stat *buf)
        int fd           = (int)sign_extend41(syscall_arg(1, 2));
        unsigned bufaddr = syscall_arg(2, 2) & BITS(15);
        struct stat st;
        if (::fstat(fd, &st) < 0)
            sys_err(errno);
        else {
            store_stat(machine, bufaddr, st);
            sys_ok(0);
        }
        break;
    }

    case SYS_seek: {
        // off_t lseek(int fd, off_t off, int whence): one-word offset.
        int fd       = (int)sign_extend41(syscall_arg(1, 3));
        off_t off    = (off_t)sign_extend41(syscall_arg(2, 3));
        int whence   = (int)sign_extend41(syscall_arg(3, 3));
        sys_ret((int64_t)::lseek(fd, off, whence));
        break;
    }

    case SYS_getpid:
        // v7 returns the parent's pid as the second result.
        sys_ok2(::getpid(), ::getppid());
        break;

    case SYS_setuid:
        sys_ret(::setuid((int)sign_extend41(syscall_arg(1, 1))));
        break;

    case SYS_getuid:
        // v7 returns the effective uid as the second result.
        sys_ok2(::getuid(), ::geteuid());
        break;

    case SYS_setgid:
        sys_ret(::setgid((int)sign_extend41(syscall_arg(1, 1))));
        break;

    case SYS_getgid:
        // v7 returns the effective gid as the second result.
        sys_ok2(::getgid(), ::getegid());
        break;

    case SYS_stime:
        // stime(): cannot set host time from a user process; ignore (apout).
        sys_ok(0);
        break;

    case SYS_alarm:
        // unsigned alarm(unsigned sec)
        sys_ok(::alarm((unsigned)(syscall_arg(1, 1) & BITS48)));
        break;

    case SYS_pause:
        // int pause(void): returns -1/EINTR when a signal arrives.
        //
        // Look for one that has already arrived FIRST.  sim_sigcatch() only records
        // it, so a signal raised while the guest was in user code leaves nothing for
        // ::pause() to wake on and this would hang -- where the kernel's pause()
        // cannot, its sleep() being woken by the same psignal() that sets p_sig.
        // check_signals() runs it at the end of the call either way.
        if (!signal_pending()) {
            ::pause();
        }
        sys_err(EINTR);
        break;

    case SYS_utime: {
        // int utime(char *path, struct { time_t actime, modtime; } *times)
        std::string path = mem_get_string(syscall_arg(1, 2));
        unsigned tp      = syscall_arg(2, 2) & BITS(15);
        if (tp == 0)
            sys_ret(::utime(path.c_str(), nullptr));
        else {
            struct utimbuf ub;
            ub.actime  = sign_extend41(machine.mem_load(tp));
            ub.modtime = sign_extend41(machine.mem_load(tp + 1));
            sys_ret(::utime(path.c_str(), &ub));
        }
        break;
    }

    case SYS_access: {
        // int access(char *path, int mode)
        std::string path = mem_get_string(syscall_arg(1, 2));
        int mode         = (int)sign_extend41(syscall_arg(2, 2));
        sys_ret(::access(path.c_str(), mode));
        break;
    }

    case SYS_nice: {
        // int nice(int incr): -1 is a legal result, so check errno.
        errno  = 0;
        int r  = ::nice((int)sign_extend41(syscall_arg(1, 1)));
        if (r == -1 && errno != 0)
            sys_err(errno);
        else
            sys_ok(r);
        break;
    }

    case SYS_ftime: {
        // ftime(struct timeb *): time, millitm, timezone, dstflag.
        unsigned bp = core.ACC & BITS(15);
        machine.mem_store(bp + 0, (Word)::time(nullptr) & BITS41);
        machine.mem_store(bp + 1, 0);
        machine.mem_store(bp + 2, 0);
        machine.mem_store(bp + 3, 0);
        sys_ok(0);
        break;
    }

    case SYS_sync:
        ::sync();
        sys_ok(0);
        break;

    case SYS_kill: {
        // int kill(int pid, int sig)
        int pid = (int)sign_extend41(syscall_arg(1, 2));
        int sig = (int)sign_extend41(syscall_arg(2, 2));
        sys_ret(::kill(pid, sig));
        break;
    }

    case SYS_dup: {
        // int dup(int fd), int dup2(int fd, int fd2)
        //
        // One entry for both, v7 style: bit 0100 of the first argument asks for
        // dup2 and the second argument is then the descriptor to duplicate onto.
        // The kernel does exactly this (dup() in kernel/sys3.c), so libc always
        // calls with two arguments and the shape is the same on both.
        int fd  = (int)sign_extend41(syscall_arg(1, 2));
        int fd2 = (int)sign_extend41(syscall_arg(2, 2));
        int m   = fd & ~077;
        fd &= 077;
        if (m & 0100)
            sys_ret(::dup2(fd, fd2));
        else
            sys_ret(::dup(fd));
        break;
    }

    case SYS_pipe: {
        // pipe(): no arguments.  The two descriptors come back as the two
        // results -- read end in the accumulator, write end in r12 -- rather
        // than through a user buffer, matching the kernel's pipe() in
        // kernel/pipe.c (u_r.r_val1 / u_r.r_val2).
        int fd[2];
        if (::pipe(fd) < 0) {
            sys_err(errno);
            break;
        }
        sys_ok2(fd[0], fd[1]);
        break;
    }

    case SYS_times: {
        // clock_t times(struct tms *): 4 one-word fields.
        unsigned bp = core.ACC & BITS(15);
        struct tms tb;
        clock_t r = ::times(&tb);
        machine.mem_store(bp + 0, (Word)tb.tms_utime & BITS41);
        machine.mem_store(bp + 1, (Word)tb.tms_stime & BITS41);
        machine.mem_store(bp + 2, (Word)tb.tms_cutime & BITS41);
        machine.mem_store(bp + 3, (Word)tb.tms_cstime & BITS41);
        sys_ret((int64_t)r);
        break;
    }

    case SYS_signal: {
        // signal(int sig, void (*func)()): the disposition is remembered HERE, and
        // the host is told only how to react on our behalf -- SIG_DFL and SIG_IGN it
        // can implement itself, and for a guest handler it gets sim_sigcatch(), which
        // records the arrival so that check_signals() can run the guest's code at the
        // next syscall return.  A guest handler is guest machine code and could not
        // run in a host signal context, which is what used to make this EINVAL.
        //
        // The value returned is the previous GUEST disposition, as the kernel's ssig()
        // returns the previous u_signal[] (kernel/sys4.c) -- including the SIG_DFL that
        // delivery resets a caught signal to.
        int sig   = (int)sign_extend41(syscall_arg(1, 2));
        Word func = core.ACC & BITS48;
        if (sig <= 0 || sig >= (int)GUEST_NSIG || sig == SIGKILL) {
            sys_err(EINVAL);
            break;
        }
        void (*disp)(int);
        if (func == 0)
            disp = SIG_DFL;
        else if (func == 1)
            disp = SIG_IGN;
        else
            disp = sim_sigcatch;

        // cppcheck wants a "pointer to const" here, but for a function pointer
        // that would be const void (*)(int) -- a pointer to a function returning
        // const void, which signal() cannot be assigned to.
        // cppcheck-suppress constVariablePointer
        void (*old)(int) = ::signal(sig, disp);
        if (old == SIG_ERR) {
            sys_err(errno);
            break;
        }
        Word olddisp       = guest_handler[sig];
        guest_handler[sig] = func;
        sys_ok((int64_t)olddisp);
        break;
    }

    case SYS_sigret:
        // The return half of the signal frame.  Nothing is reported: the frame it
        // has just reloaded holds the accumulator, r12 and r14 the interrupted code
        // must resume with.
        sys_sigret();
        break;

    case SYS_umask:
        // int umask(int mask)
        sys_ok(::umask((int)sign_extend41(syscall_arg(1, 1)) & 0777));
        break;

    case SYS_chroot:
        // int chroot(char *path)
        sys_ret(::chroot(mem_get_string(syscall_arg(1, 1)).c_str()));
        break;

    case SYS_stty:
    case SYS_gtty: {
        // Terminal parameters: honoured only as far as "is this a tty".
        int fd = (int)sign_extend41(syscall_arg(1, 2));
        if (!isatty(fd))
            sys_err(ENOTTY);
        else if (num == SYS_gtty) {
            unsigned bp = syscall_arg(2, 2) & BITS(15);
            for (unsigned i = 0; i < 5; i++)
                machine.mem_store(bp + i, 0);
            sys_ok(0);
        } else
            sys_ok(0);
        break;
    }

    case SYS_ioctl:
        // Accepted as a no-op success, like apout for non-tty-param requests.
        sys_ok(0);
        break;

    case SYS_lock:
        // Ignored; return success (apout).
        sys_ok(0);
        break;

    case SYS_mount:
    case SYS_umount:
    case SYS_ptrace:
    case SYS_profil:
    case SYS_acct:
    case SYS_phys:
        // Not meaningful for a user-level simulator.
        sys_err(EPERM);
        break;

    default:
        throw Exception("Unimplemented syscall " + std::to_string(num));
    }

    // Callee stack cleanup (doc/Besm6_Calling_Conventions.md): the caller pushed
    // N-1 arguments just below r15 (the Nth argument travels in the accumulator
    // and is never pushed).  The bare `$77 N` trap stands in for the called
    // function, so it must decrement r15 by N-1 exactly as a c/ret epilogue
    // would; with 0 or 1 arguments there is nothing on the stack to pop.
    unsigned n = syscall_nargs(num);
    if (n >= 2)
        core.M[017] = ADDR(core.M[017] - (n - 1));

    // ...and then deliver a signal, if one is pending and the guest catches it.
    // This is the kernel's delivery point -- sysret() in kernel/syscall.c, after
    // the same stack cleanup -- so the frame is built on the same r15.
    check_signals();
}
