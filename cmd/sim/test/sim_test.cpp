//
// Unit tests for the BESM-6 simulator core: the Unix v7 syscall trap
// (extracode 077) and illegal-instruction handling.
//
// The tests poke raw instruction words into memory and run the machine
// directly, avoiding a dependency on the assembler/linker.  A 48-bit word
// packs two 24-bit instructions: the left one (bits 48..25) executes first,
// then the right one (bits 24..1).
//
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "machine.h"
#include "memory.h"

// The a.out I/O helpers are plain C; pull them in with C linkage (as machine.cpp does).
extern "C" {
#include "besm6/b.out.h"
}

//
// Program entry point: BADDR = HDRSZ / W = 48 / 6 = 8.
//
static const unsigned ENTRY = 8;

//
// Scratch stack area and a couple of scratch data areas used by the tests.
//
static const unsigned STACK = 0x200;

//
// Encode a short instruction: register modifier, opcode (000..077), address.
//
static unsigned insn(unsigned reg, unsigned opcode, unsigned addr)
{
    return (reg << 20) | (opcode << 12) | (addr & 07777);
}

//
// Pack two 24-bit instructions into one word (left executes first).
//
static Word word(unsigned left, unsigned right)
{
    return ((Word)left << 24) | right;
}

// $77 N — extracode 077 (Unix syscall) with syscall number N.
static unsigned syscall_insn(unsigned num)
{
    return insn(0, 077, num);
}

// стоп/stop — halt the processor so registers can be inspected after a syscall.
static unsigned stop_insn()
{
    return 0330u << 12;
}

//
// Syscall numbers used below (kernel/sysent.c).
//
enum {
    SYS_exit   = 1,
    SYS_read   = 3,
    SYS_write  = 4,
    SYS_open   = 5,
    SYS_close  = 6,
    SYS_wait   = 7,
    SYS_creat  = 8,
    SYS_time   = 13,
    SYS_break  = 17,
    SYS_stat   = 18,
    SYS_seek   = 19,
    SYS_getpid = 20,
    SYS_getuid = 24,
    SYS_getgid = 47,
    SYS_pipe   = 42,
    SYS_fork   = 2,
    SYS_dup    = 41,
    SYS_kill   = 37,
    SYS_sigret = 45,
    SYS_signal = 48,
};

//
// Write a NUL-terminated string into memory at word `addr`; return a char*
// fat pointer to it (bit 48 set, offset field 5 = byte #0, word address).
//
static Word put_string(Memory &m, unsigned addr, const char *s)
{
    BytePointer bp(m, addr, 0);
    for (; *s; s++)
        bp.put_byte((uint8_t)*s);
    bp.put_byte(0);
    return BIT48 | (5ull << 44) | addr;
}

// Read `n` bytes of a char array from memory starting at word `addr`.
static std::string get_bytes(Memory &m, unsigned addr, unsigned n)
{
    BytePointer bp(m, addr, 0);
    std::string s;
    for (unsigned i = 0; i < n; i++)
        s.push_back((char)bp.get_byte());
    return s;
}

//
// Run a single syscall: place args 1..N-1 on the stack, the last arg in the
// accumulator, execute `$77 num` followed by `stop`, and leave the result in
// the accumulator / errno in M[14] for inspection.
//
static void run_syscall(Machine &m, unsigned num, const std::vector<Word> &stackargs, Word acc)
{
    for (size_t i = 0; i < stackargs.size(); i++)
        m.memory.store(STACK + i, stackargs[i]);
    m.cpu.set_m(017, STACK + stackargs.size());
    m.cpu.set_acc(acc);
    // The extracode returns to the LEFT half of the next word, so the halt goes
    // there; the right half beside the $77 is inert padding that never runs.
    m.memory.store(ENTRY, word(syscall_insn(num), 0));
    m.memory.store(ENTRY + 1, word(stop_insn(), 0));
    m.cpu.set_pc(ENTRY);
    m.run();
}

// -1 as a guest int (41-bit two's complement).
static const Word GUEST_MINUS_ONE = BITS41;

//
// _exit(status) returns the status through Machine::get_exit_status().
//
TEST(Syscall, ExitStatus)
{
    Memory memory;
    Machine machine{ memory };

    // exit in the left half; the right half is never reached.
    memory.store(ENTRY, word(syscall_insn(SYS_exit), 0));
    machine.cpu.set_pc(ENTRY);
    machine.cpu.set_acc(0);

    machine.run();
    EXPECT_EQ(machine.get_exit_status(), 0);
}

TEST(Syscall, ExitNonZeroStatus)
{
    Memory memory;
    Machine machine{ memory };

    memory.store(ENTRY, word(syscall_insn(SYS_exit), 0));
    machine.cpu.set_pc(ENTRY);
    machine.cpu.set_acc(5);

    machine.run();
    EXPECT_EQ(machine.get_exit_status(), 5);
}

//
// --status prints the _exit() value as a 41-bit signed integer on stdout.
//
TEST(Syscall, ExitStatusReport)
{
    // A small positive value prints as itself.
    {
        Memory memory;
        Machine machine{ memory };
        machine.set_report_status(true);

        memory.store(ENTRY, word(syscall_insn(SYS_exit), 0));
        machine.cpu.set_pc(ENTRY);
        machine.cpu.set_acc(5);

        testing::internal::CaptureStdout();
        machine.run();
        EXPECT_EQ(testing::internal::GetCapturedStdout(), "5\n");
        EXPECT_EQ(machine.get_exit_status(), 5);
    }

    // A value with bit 41 set is sign-extended: -1, not 255.
    {
        Memory memory;
        Machine machine{ memory };
        machine.set_report_status(true);

        memory.store(ENTRY, word(syscall_insn(SYS_exit), 0));
        machine.cpu.set_pc(ENTRY);
        machine.cpu.set_acc(GUEST_MINUS_ONE);

        testing::internal::CaptureStdout();
        machine.run();
        EXPECT_EQ(testing::internal::GetCapturedStdout(), "-1\n");
        // The host process return code is still the low byte.
        EXPECT_EQ(machine.get_exit_status(), 0xff);
    }
}

//
// Without --status, the _exit() value is not printed.
//
TEST(Syscall, ExitStatusNoReport)
{
    Memory memory;
    Machine machine{ memory };

    memory.store(ENTRY, word(syscall_insn(SYS_exit), 0));
    machine.cpu.set_pc(ENTRY);
    machine.cpu.set_acc(5);

    testing::internal::CaptureStdout();
    machine.run();
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
}

//
// write(1, "hi\n", 3) sends three bytes to stdout and returns the count.
//
TEST(Syscall, Write)
{
    Memory memory;
    Machine machine{ memory };

    Word buf = put_string(memory, 0x300, "hi\n");

    testing::internal::CaptureStdout();
    run_syscall(machine, SYS_write, { 1, buf }, 3);
    fflush(stdout);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(out, "hi\n");
    EXPECT_EQ(machine.cpu.get_acc(), 3u);
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
}

//
// The $77 trap performs the callee stack cleanup: on return r15 (M[017]) is
// decremented by N-1 (the words the caller pushed), per the BESM-6 calling
// convention.  With 0 or 1 arguments nothing is on the stack, so r15 is
// unchanged.
//
TEST(Syscall, StackCleanup)
{
    Memory memory;
    Machine machine{ memory };

    // 3-argument write: two words pushed, r15 drops by 2 back to STACK.
    Word buf = put_string(memory, 0x300, "hi\n");
    testing::internal::CaptureStdout();
    run_syscall(machine, SYS_write, { 1, buf }, 3);
    testing::internal::GetCapturedStdout();
    EXPECT_EQ(machine.cpu.get_m(017), STACK);

    // 2-argument open of a nonexistent path: one word pushed, r15 drops by 1.
    Word path = put_string(memory, 0x300, "/no/such/file");
    run_syscall(machine, SYS_open, { path }, 0);
    EXPECT_EQ(machine.cpu.get_acc(), GUEST_MINUS_ONE);
    EXPECT_EQ(machine.cpu.get_m(017), STACK);

    // 0-argument getpid: nothing pushed, r15 left unchanged.
    run_syscall(machine, SYS_getpid, {}, 0);
    EXPECT_EQ(machine.cpu.get_m(017), STACK);

    // pipe() takes no arguments either, now that both descriptors come back in
    // registers rather than through a user buffer: r15 must not move.
    run_syscall(machine, SYS_pipe, {}, 0);
    ::close((int)machine.cpu.get_acc());
    ::close((int)machine.cpu.get_m(12));
    EXPECT_EQ(machine.cpu.get_m(017), STACK);
}

//
// getpid() returns the host pid, and the parent's pid as v7's second result in
// r12 (R_VAL2; see include/sys/reg.h).  r12 is an index register -- 15 bits --
// so a host ppid above 32767 arrives truncated; that is the ABI, and a v7 guest
// never generates a pid that large.
//
TEST(Syscall, Getpid)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_getpid, {}, 0);
    EXPECT_EQ((long)machine.cpu.get_acc(), (long)getpid());
    EXPECT_EQ((long)machine.cpu.get_m(12), (long)(getppid() & 077777));
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
}

//
// getuid()/getgid() return the real id in the accumulator and the effective one
// in r12, as v7 does.
//
TEST(Syscall, Getuid)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_getuid, {}, 0);
    EXPECT_EQ((long)machine.cpu.get_acc(), (long)getuid());
    EXPECT_EQ((long)machine.cpu.get_m(12), (long)geteuid());
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
}

TEST(Syscall, Getgid)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_getgid, {}, 0);
    EXPECT_EQ((long)machine.cpu.get_acc(), (long)getgid());
    EXPECT_EQ((long)machine.cpu.get_m(12), (long)getegid());
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
}

//
// time() returns a plausible epoch value.
//
TEST(Syscall, Time)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_time, {}, 0);
    // Well past 2020-01-01 (1577836800).
    EXPECT_GT((long)machine.cpu.get_acc(), 1577836800L);
}

//
// open() of a nonexistent path returns -1 and ENOENT in M[14].
//
TEST(Syscall, OpenMissing)
{
    Memory memory;
    Machine machine{ memory };

    Word path = put_string(memory, 0x300, "/no/such/file/b6sim");
    run_syscall(machine, SYS_open, { path }, 0);

    EXPECT_EQ(machine.cpu.get_acc(), GUEST_MINUS_ONE);
    EXPECT_EQ(machine.cpu.get_m(14), 2u); // ENOENT
}

//
// A host errno with no v7 equivalent never reaches the guest.  A path component
// past NAME_MAX gives ENAMETOOLONG on every modern host -- 63 on macOS, 36 on
// Linux, neither of them a number include/errno.h defines -- and guest_errno()
// folds it onto ENOENT: the path names nothing a v7 system could have had.
//
TEST(Syscall, OpenNameTooLong)
{
    Memory memory;
    Machine machine{ memory };

    std::string longname = "/" + std::string(600, 'x');
    Word path            = put_string(memory, 0x300, longname.c_str());
    run_syscall(machine, SYS_open, { path }, 0);

    EXPECT_EQ(machine.cpu.get_acc(), GUEST_MINUS_ONE);
    EXPECT_EQ(machine.cpu.get_m(14), 2u); // ENOENT, not the host's ENAMETOOLONG
}

//
// creat -> write -> close -> open -> lseek -> read round-trips file contents.
//
TEST(Syscall, FileRoundTrip)
{
    Memory memory;
    Machine machine{ memory };

    char path[] = "/tmp/b6sim_testXXXXXX";
    int tfd     = mkstemp(path);
    ASSERT_GE(tfd, 0);
    ::close(tfd);

    Word ppath = put_string(memory, 0x300, path);
    Word wbuf  = put_string(memory, 0x400, "DATA");

    // creat(path, 0644)
    run_syscall(machine, SYS_creat, { ppath }, 0644);
    int fd = (int)machine.cpu.get_acc();
    ASSERT_GE(fd, 0);

    // write(fd, "DATA", 4)
    run_syscall(machine, SYS_write, { (Word)fd, wbuf }, 4);
    EXPECT_EQ(machine.cpu.get_acc(), 4u);

    // close(fd)
    run_syscall(machine, SYS_close, {}, (Word)fd);
    EXPECT_EQ(machine.cpu.get_acc(), 0u);

    // open(path, 0 /*O_RDONLY*/)
    run_syscall(machine, SYS_open, { ppath }, 0);
    int rfd = (int)machine.cpu.get_acc();
    ASSERT_GE(rfd, 0);

    // read(rfd, buf, 4) into a fresh area
    Word rbuf = BIT48 | (5ull << 44) | 0x500;
    run_syscall(machine, SYS_read, { (Word)rfd, rbuf }, 4);
    EXPECT_EQ(machine.cpu.get_acc(), 4u);
    EXPECT_EQ(get_bytes(memory, 0x500, 4), "DATA");

    run_syscall(machine, SYS_close, {}, (Word)rfd);
    ::unlink(path);
}

//
// dup and dup2 share one entry, v7 style: the call always takes TWO arguments,
// and bit 0100 of the first asks for dup2 with the second naming the target
// descriptor.  The kernel's dup() reads the same two-field argument struct
// (kernel/sys3.c), so libc/sys/dup.s pushes the extra word for plain dup too --
// which is what makes the stack cleanup a one-word pop either way.
//
TEST(Syscall, Dup)
{
    Memory memory;
    Machine machine{ memory };

    // A descriptor to duplicate: the read end of a fresh pipe.
    run_syscall(machine, SYS_pipe, {}, 0);
    int rfd = (int)machine.cpu.get_acc();
    int wfd = (int)machine.cpu.get_m(12);
    ASSERT_GE(rfd, 0);

    // dup(rfd): the 0100 bit clear, the second argument unused.
    run_syscall(machine, SYS_dup, { (Word)rfd }, 0);
    int dfd = (int)machine.cpu.get_acc();
    ASSERT_GE(dfd, 0);
    EXPECT_NE(dfd, rfd);
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
    EXPECT_EQ(machine.cpu.get_m(017), STACK); // two arguments: one word popped

    // dup2(rfd, dfd): the 0100 bit set.  Free the slot first, so the result is
    // the requested number and not merely a legal one.
    ::close(dfd);
    run_syscall(machine, SYS_dup, { (Word)(rfd | 0100) }, (Word)dfd);
    EXPECT_EQ((int)machine.cpu.get_acc(), dfd);
    EXPECT_EQ(machine.cpu.get_m(14), 0u);

    // A closed descriptor is -1/EBADF, not a wild duplicate.  63 and not 99:
    // the first argument keeps only its low six bits, v7 having spent the rest
    // on the dup2 flag.
    run_syscall(machine, SYS_dup, { 63 }, 0);
    EXPECT_EQ(machine.cpu.get_acc(), GUEST_MINUS_ONE);
    EXPECT_EQ(machine.cpu.get_m(14), 9u); // EBADF

    ::close(rfd);
    ::close(wfd);
    ::close(dfd);
}

//
// stat() fills the 11-word struct; check the size and the regular-file bit.
//
TEST(Syscall, Stat)
{
    Memory memory;
    Machine machine{ memory };

    char path[] = "/tmp/b6sim_statXXXXXX";
    int tfd     = mkstemp(path);
    ASSERT_GE(tfd, 0);
    ASSERT_EQ(::write(tfd, "hello", 5), 5);
    ::close(tfd);

    Word ppath       = put_string(memory, 0x300, path);
    const unsigned S = 0x400;
    run_syscall(machine, SYS_stat, { ppath }, S);

    EXPECT_EQ(machine.cpu.get_acc(), 0u);
    // st_mode is word #2, st_size is word #7 (see include/sys/stat.h order).
    EXPECT_EQ(memory.load(S + 7), 5u);
    EXPECT_TRUE((memory.load(S + 2) & 0170000) == 0100000); // S_IFREG

    ::unlink(path);
}

//
// break() rounds the requested break up to a page boundary and rejects growth
// into the stack.
//
TEST(Syscall, Break)
{
    Memory memory;
    Machine machine{ memory };

    // break() takes its single argument in the accumulator.  Seed the stack
    // pointer at the reserved base so page-aligned breaks below it are accepted.
    auto run_break = [&](Word addr) {
        machine.cpu.set_m(017, STACK_BASE);
        machine.cpu.set_acc(addr);
        machine.memory.store(ENTRY, word(syscall_insn(SYS_break), 0));
        machine.memory.store(ENTRY + 1, word(stop_insn(), 0));
        machine.cpu.set_pc(ENTRY);
        machine.run();
    };

    // An unaligned break below the stack is rounded up to the next page.
    run_break(0x401);
    EXPECT_EQ(machine.cpu.get_acc(), 0u);
    EXPECT_EQ(machine.get_program_break(), 0x800u);

    // A break that reaches the stack base is rejected.
    run_break(STACK_BASE);
    EXPECT_EQ(machine.cpu.get_acc(), GUEST_MINUS_ONE);
    EXPECT_EQ(machine.cpu.get_m(14), 12u); // ENOMEM
}

//
// pipe() returns the two fds as its two results -- read end in the accumulator,
// write end in r12 (R_VAL2) -- and data written to one is read back from the
// other.
//
TEST(Syscall, Pipe)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_pipe, {}, 0);
    EXPECT_EQ(machine.cpu.get_m(14), 0u);

    int rfd = (int)machine.cpu.get_acc();
    int wfd = (int)machine.cpu.get_m(12);
    ASSERT_GE(rfd, 0);
    ASSERT_GE(wfd, 0);
    ASSERT_NE(rfd, wfd);

    // Host writes into the write end; the guest reads from the read end.
    ASSERT_EQ(::write(wfd, "xy", 2), 2);
    Word rbuf = BIT48 | (5ull << 44) | 0x500;
    run_syscall(machine, SYS_read, { (Word)rfd, rbuf }, 2);
    EXPECT_EQ(machine.cpu.get_acc(), 2u);
    EXPECT_EQ(get_bytes(memory, 0x500, 2), "xy");

    ::close(rfd);
    ::close(wfd);
}

//
// fork()/wait(): the child exits with a status the parent collects.  Both use
// v7's two-value return -- fork says which side you are in r12, and wait brings
// the pid back in the accumulator with the status in r12.
//
TEST(Syscall, ForkWait)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_fork, {}, 0);
    if (machine.cpu.get_m(12) == 1) {
        // Forked child: leave the gtest harness immediately with a known code.
        _exit(42);
    }
    Word child = machine.cpu.get_acc();
    ASSERT_GT((long)child, 0);

    run_syscall(machine, SYS_wait, {}, 0);
    EXPECT_EQ(machine.cpu.get_acc(), child); // pid of the reaped child
    EXPECT_EQ((machine.cpu.get_m(12) >> 8) & 0xff, 42u);
}

//
// An unimplemented syscall terminates with an error.
//
TEST(Syscall, UnimplementedTerminates)
{
    Memory memory;
    Machine machine{ memory };

    memory.store(ENTRY, word(syscall_insn(99), 0));
    machine.cpu.set_pc(ENTRY);

    EXPECT_THROW(machine.run(), std::runtime_error);
}

//
// An extracode returns to the LEFT HALF of the next word, whichever half it
// occupied: an instruction packed beside a left-half extracode never executes.
// See doc/Dubna_Context_Switch.md §9.
//
TEST(Cpu, ExtracodeReturnsToLeftHalfOfNextWord)
{
    Memory memory;
    Machine machine{ memory };

    // ENTRY:     $77 20 (getpid) | stop   <- the stop must NOT execute
    // ENTRY + 1: 13 vtm 077      | stop   <- execution must resume here
    memory.store(ENTRY, word(syscall_insn(SYS_getpid), stop_insn()));
    memory.store(ENTRY + 1, word(insn(013, 0240, 077), stop_insn()));
    machine.cpu.set_pc(ENTRY);
    machine.run();

    // Had the right half beside the extracode run, the machine would have
    // halted there and M[11] would still be 0.
    EXPECT_EQ(machine.cpu.get_m(11), 077u);
    EXPECT_EQ(machine.cpu.get_m(14), 0u); // getpid succeeded
}

//
// An extracode in a RIGHT half is unaffected: the same rule lands it on the next
// word, which is where the plain half-step would have gone anyway.
//
TEST(Cpu, ExtracodeInRightHalfReturnsToNextWord)
{
    Memory memory;
    Machine machine{ memory };

    // ENTRY:     13 vtm 011 | $77 20 (getpid)
    // ENTRY + 1: 12 vtm 022 | stop
    memory.store(ENTRY, word(insn(013, 0240, 011), syscall_insn(SYS_getpid)));
    memory.store(ENTRY + 1, word(insn(012, 0240, 022), stop_insn()));
    machine.cpu.set_pc(ENTRY);
    machine.run();

    EXPECT_EQ(machine.cpu.get_m(11), 011u); // ran before the extracode
    EXPECT_EQ(machine.cpu.get_m(10), 022u); // ran after it
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
}

//
// An illegal instruction terminates the simulation with an error.
//
TEST(Cpu, IllegalInstructionTerminates)
{
    Memory memory;
    Machine machine{ memory };

    // Opcode 002 (рег/mod) is illegal in user mode.
    memory.store(ENTRY, word(insn(0, 002, 0), 0));
    machine.cpu.set_pc(ENTRY);

    EXPECT_THROW(machine.run(), std::runtime_error);
}

//
// Write a minimal FMAGIC executable: one text word, no const/data/bss.  Its
// single instruction is never executed -- these tests inspect the loaded image
// rather than running it.
//
static void write_minimal_aout(const char *path)
{
    struct exec hdr = {};
    hdr.a_magic     = FMAGIC;
    hdr.a_text      = 6; // one 6-byte word
    hdr.a_entry     = HDRSZ / 6;
    FILE *f         = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    fputhdr(&hdr, f);
    fputw(0, f);
    fclose(f);
}

//
// Read a NUL-terminated string through a char* fat pointer, the way the guest's
// own dereference would: word address in bits 15..1, byte-offset field in bits
// 47..45 with 5 naming byte #0 (the decode in cmd/sim/syscall.cpp).
//
static std::string get_string_at(Memory &m, Word fatptr)
{
    BytePointer bp(m, (unsigned)(fatptr & BITS(15)), 5 - (unsigned)((fatptr >> 44) & 7));
    std::string s;
    for (;;) {
        uint8_t c = bp.get_byte();
        if (c == 0)
            break;
        s.push_back((char)c);
    }
    return s;
}

//
// After loading an executable the stack pointer r15 (M[017]) is seeded at the
// base of the reserved stack region, disjoint from the heap.  load_program() is
// the bare image loader: the argument block is exec()'s business.
//
TEST(Cpu, StackSeededAtBase)
{
    const char *path = "test_stackseed.bout";
    write_minimal_aout(path);

    Memory memory;
    Machine machine{ memory };
    machine.load_program(path);
    EXPECT_EQ(machine.cpu.get_m(017), STACK_BASE);

    std::remove(path);
}

//
// exec() lays the kernel's argument block at the fixed base 070000: argc, the
// argv[] and envp[] vectors of fat pointers each closed by a null word, then the
// strings packed six to a word, then a closing word; r15 starts just above it.
// This is exece() in kernel/sys1.c, word for word, so one crt0 serves both.
//
TEST(Cpu, ExecArgBlock)
{
    const char *path = "test_execblock.bout";
    write_minimal_aout(path);

    Memory memory;
    Machine machine{ memory };
    machine.exec(path, { "prog", "a", "bb" }, { "X=1" });

    // argc, three argv slots, a null, one envp slot, a null.
    EXPECT_EQ(memory.load(STACK_BASE), 3u);
    EXPECT_EQ(memory.load(STACK_BASE + 4), 0u);
    EXPECT_EQ(memory.load(STACK_BASE + 6), 0u);

    // The strings begin just above the vectors, at 070000 + 1 + 4 + 2.
    const unsigned S = STACK_BASE + 7;

    // Each pointer is the live byte cursor: the strings are NOT re-aligned
    // between one and the next, so only argv[0] starts at byte #0 (field 5).
    // "prog\0" fills bytes 0..4 of S, "a\0" straddles into S+1, and so on.
    EXPECT_EQ(memory.load(STACK_BASE + 1), BIT48 | (5ull << 44) | S);       // "prog"
    EXPECT_EQ(memory.load(STACK_BASE + 2), BIT48 | (0ull << 44) | S);       // "a"
    EXPECT_EQ(memory.load(STACK_BASE + 3), BIT48 | (4ull << 44) | (S + 1)); // "bb"
    EXPECT_EQ(memory.load(STACK_BASE + 5), BIT48 | (1ull << 44) | (S + 1)); // "X=1"

    // What the guest reads back through those pointers.
    EXPECT_EQ(get_string_at(memory, memory.load(STACK_BASE + 1)), "prog");
    EXPECT_EQ(get_string_at(memory, memory.load(STACK_BASE + 2)), "a");
    EXPECT_EQ(get_string_at(memory, memory.load(STACK_BASE + 3)), "bb");
    EXPECT_EQ(get_string_at(memory, memory.load(STACK_BASE + 5)), "X=1");

    // 14 bytes of strings end mid-word in S+2; the closing word is the next one.
    EXPECT_EQ(memory.load(S + 3), 0u);
    EXPECT_EQ(machine.cpu.get_m(017), S + 4);

    std::remove(path);
}

//
// With neither arguments nor environment the block is still four words -- argc
// and the two vectors' nulls and the closing word -- and r15 sits above them.
//
TEST(Cpu, ExecEmptyArgBlock)
{
    const char *path = "test_execempty.bout";
    write_minimal_aout(path);

    Memory memory;
    Machine machine{ memory };
    machine.exec(path, {}, {});

    for (unsigned i = 0; i < 4; i++)
        EXPECT_EQ(memory.load(STACK_BASE + i), 0u) << "word " << i;
    EXPECT_EQ(machine.cpu.get_m(017), STACK_BASE + 4);

    std::remove(path);
}

//
// Nothing is handed to the new image in a register: setregs() (kernel/sys1.c)
// zeroes ACC and r1..r14 and sets only r15 and the return address.
//
TEST(Cpu, ExecClearsRegisters)
{
    const char *path = "test_execregs.bout";
    write_minimal_aout(path);

    Memory memory;
    Machine machine{ memory };

    // Dirty every register the new image must not inherit.
    machine.cpu.set_acc(0'7777'7777'7777'7777);
    for (unsigned i = 1; i <= 14; i++)
        machine.cpu.set_m(i, 077777);

    machine.exec(path, { "prog" }, {});

    EXPECT_EQ(machine.cpu.get_acc(), 0u);
    for (unsigned i = 1; i <= 14; i++)
        EXPECT_EQ(machine.cpu.get_m(i), 0u) << "r" << i;

    std::remove(path);
}

//
// The argument block lives at the base of the STACK; the heap break stays where
// load_program() put it, on the first page boundary above the bss.
//
TEST(Cpu, ExecKeepsProgramBreak)
{
    const char *path = "test_execbreak.bout";
    write_minimal_aout(path);

    Memory memory;
    Machine machine{ memory };
    machine.exec(path, { "prog", "argument" }, { "X=1" });

    EXPECT_EQ(machine.get_program_break(), PAGE_NWORDS);

    std::remove(path);
}

//
// An argument list past NCARGS (5120 bytes, include/sys/param.h) is refused
// rather than scribbled over the stack.
//
TEST(Cpu, ExecArgListTooLong)
{
    const char *path = "test_execbig.bout";
    write_minimal_aout(path);

    Memory memory;
    Machine machine{ memory };
    std::vector<std::string> argv(6, std::string(1000, 'x'));

    EXPECT_THROW(machine.exec(path, argv, {}), std::runtime_error);

    std::remove(path);
}

//
// A store to the guard word 077777 through the stack register traps with a
// "stack protection violation" (surfaced as std::runtime_error).
//
TEST(Cpu, StackGuardTrapsStore)
{
    Memory memory;
    Machine machine{ memory };

    // atx (000) with reg 017, addr 0: store ACC at M[017], then post-increment.
    memory.store(ENTRY, word(insn(017, 000, 0), stop_insn()));
    machine.cpu.set_m(017, STACK_LIMIT);
    machine.cpu.set_pc(ENTRY);

    EXPECT_THROW(machine.run(), std::runtime_error);
}

//
// A load from the guard word 077777 through the stack register traps too.
//
TEST(Cpu, StackGuardTrapsLoad)
{
    Memory memory;
    Machine machine{ memory };

    // xta (010) with reg 017, addr 1: Aex = 1 + M[017] = 077777 (nonzero addr
    // avoids the addr==0 stack-pop that would step off the guard word).
    memory.store(ENTRY, word(insn(017, 010, 1), stop_insn()));
    machine.cpu.set_m(017, STACK_LIMIT - 1);
    machine.cpu.set_pc(ENTRY);

    EXPECT_THROW(machine.run(), std::runtime_error);
}

//
// Signal delivery: the frame b6sim builds is the kernel's (kernel/sendsig.c), and the
// handler returns through the `$77 SYS_sigret' word planted just above it.
//
// The guest "handler" here is a bare `stop', so the machine halts the moment delivery
// hands it control and every register can be inspected on the way in.  Running the
// planted word afterwards is the return path, and the interrupted context has to come
// back out of it whole.
//
TEST(Syscall, SignalDelivery)
{
    // include/sys/reg.h, and the third hand-maintained copy of it (syscall.cpp holds
    // the second).
    // A trailing underscore on the signal number: <signal.h> owns the bare name.
    const unsigned NREGFRAME = 21;
    const unsigned SIGTERM_  = 15;
    const unsigned HANDLER   = 0x100;

    Memory memory;
    Machine machine{ memory };

    // The handler halts at once; delivery is what gets us there.
    memory.store(HANDLER, word(stop_insn(), 0));

    // signal(SIGTERM_, handler): the number is pushed, the handler travels in the
    // accumulator.  The previous disposition -- SIG_DFL -- comes back.
    run_syscall(machine, SYS_signal, { SIGTERM_ }, HANDLER);
    EXPECT_EQ(machine.cpu.get_acc(), 0u);
    EXPECT_EQ(machine.cpu.get_m(017), STACK);

    // kill(getpid(), SIGTERM_): the host raises it, b6sim records it, and the delivery
    // happens at the end of the call -- the kernel's point too (sysret()).
    run_syscall(machine, SYS_kill, { (Word)::getpid() }, SIGTERM_);

    // The entry ABI: the number in the accumulator, r14 = -1 (the negative argument
    // count, 15 bits wide), r13 the planted word and r15 one above it.
    EXPECT_EQ(machine.cpu.get_pc(), HANDLER);
    EXPECT_EQ(machine.cpu.get_acc(), (Word)SIGTERM_);
    EXPECT_EQ(machine.cpu.get_m(14), 077777u);
    EXPECT_EQ(machine.cpu.get_m(13), STACK + NREGFRAME);
    EXPECT_EQ(machine.cpu.get_m(017), STACK + NREGFRAME + 1);

    // The frame itself, at the r15 the call returned on: the accumulator kill() set,
    // the PC it would have resumed at, and the stack pointer it had.
    EXPECT_EQ(memory.load(STACK + 0), 0u);         // frame[0]  = ACC: kill returned 0
    EXPECT_EQ(memory.load(STACK + 3), ENTRY + 1u); // frame[3]  = RET
    EXPECT_EQ(memory.load(STACK + 6), STACK);      // frame[6]  = r15
    EXPECT_EQ(memory.load(STACK + 7), 0u);         // frame[7]  = r14: errno, 0 on success

    // And the return path: one word, `$77 SYS_sigret', which is what r13 names.
    EXPECT_EQ(memory.load(STACK + NREGFRAME), (Word)syscall_insn(SYS_sigret) << 24);

    // Run it.  sigret() reloads the frame, so the machine comes back to the halt after
    // the kill with everything the handler was free to clobber restored.
    machine.cpu.set_pc(machine.cpu.get_m(13));
    machine.run();
    EXPECT_EQ(machine.cpu.get_pc(), ENTRY + 1u);
    EXPECT_EQ(machine.cpu.get_acc(), 0u);
    EXPECT_EQ(machine.cpu.get_m(14), 0u);
    EXPECT_EQ(machine.cpu.get_m(017), STACK);

    // A caught signal is reset to SIG_DFL as it is delivered, as v7 does (psig()), so
    // signal() now answers with SIG_DFL rather than with the handler.
    run_syscall(machine, SYS_signal, { SIGTERM_ }, 1 /* SIG_IGN */);
    EXPECT_EQ(machine.cpu.get_acc(), 0u);
}
