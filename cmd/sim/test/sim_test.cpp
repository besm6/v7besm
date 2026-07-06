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
    SYS_pipe   = 42,
    SYS_fork   = 2,
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
    m.memory.store(ENTRY, word(syscall_insn(num), stop_insn()));
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
// getpid() returns the host pid.
//
TEST(Syscall, Getpid)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_getpid, {}, 0);
    EXPECT_EQ((long)machine.cpu.get_acc(), (long)getpid());
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
// break() moves the program break and rejects growth into the stack.
//
TEST(Syscall, Break)
{
    Memory memory;
    Machine machine{ memory };

    // run_syscall leaves the stack pointer at STACK for a 1-arg call.
    run_syscall(machine, SYS_break, {}, 0x100); // below the stack -> ok
    EXPECT_EQ(machine.cpu.get_acc(), 0u);
    EXPECT_EQ(machine.get_program_break(), 0x100u);

    run_syscall(machine, SYS_break, {}, 0x300); // above the stack -> ENOMEM
    EXPECT_EQ(machine.cpu.get_acc(), GUEST_MINUS_ONE);
    EXPECT_EQ(machine.cpu.get_m(14), 12u); // ENOMEM
}

//
// pipe() returns two fds; data written to one is read back from the other.
//
TEST(Syscall, Pipe)
{
    Memory memory;
    Machine machine{ memory };

    const unsigned P = 0x400;
    run_syscall(machine, SYS_pipe, {}, P);
    EXPECT_EQ(machine.cpu.get_acc(), 0u);

    int rfd = (int)memory.load(P);
    int wfd = (int)memory.load(P + 1);
    ASSERT_GE(rfd, 0);
    ASSERT_GE(wfd, 0);

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
// fork()/wait(): the child exits with a status the parent collects.
//
TEST(Syscall, ForkWait)
{
    Memory memory;
    Machine machine{ memory };

    run_syscall(machine, SYS_fork, {}, 0);
    Word acc = machine.cpu.get_acc();
    if (acc == 0) {
        // Forked child: leave the gtest harness immediately with a known code.
        _exit(42);
    }
    ASSERT_GT((long)acc, 0);

    const unsigned STP = 0x600;
    run_syscall(machine, SYS_wait, {}, STP);
    EXPECT_EQ(machine.cpu.get_acc(), acc); // pid of the reaped child
    EXPECT_EQ((memory.load(STP) >> 8) & 0xff, 42u);
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
