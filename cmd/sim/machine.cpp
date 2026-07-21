//
// BESM-6: Big Electronic Calculating Machine, model 6.
//
#include "machine.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <regex>

#include "memory.h"

//
// BESM-6 a.out object/executable format, shared with the rest of the toolchain.
// The library is plain C, so pull it in with C linkage.
//
extern "C" {
#include "besm6/b.out.h"
}

// Diagnostic prefix (basename of argv[0]); declared in session.h, set by main().
const char *sim_progname = "sim";

// Static fields.
bool Machine::verbose                    = false;
uint64_t Machine::simulated_instructions = 0;

// Limit of instructions, by default.
const uint64_t Machine::DEFAULT_LIMIT = 100ULL * 1000 * 1000 * 1000;

//
// Initialize the machine.
//
Machine::Machine(Memory &m)
    : progress_time_last(std::chrono::steady_clock::now()), memory(m), cpu(*this, m)
{
}

//
// Deallocate the machine: disable tracing.
//
Machine::~Machine()
{
    close_trace();
}

//
// Every few seconds, print a message to stderr, to track the simulation progress.
//
void Machine::show_progress()
{
    //
    // Check the real time every few thousand cycles.
    //
    static const uint64_t PROGRESS_INCREMENT = 10000;

    if (simulated_instructions >= progress_count + PROGRESS_INCREMENT) {
        progress_count += PROGRESS_INCREMENT;

        // How much time has passed since the last check?
        auto time_now = std::chrono::steady_clock::now();
        auto delta    = time_now - progress_time_last;
        auto sec      = std::chrono::duration_cast<std::chrono::seconds>(delta).count();

        // Emit message every 5 seconds.
        if (sec >= 5) {
            std::cerr << "----- Progress " << simulated_instructions << " -----" << std::endl;
            progress_time_last = time_now;
        }
    }
}

//
// Run the machine until completion.
//
void Machine::run()
{
    // Show initial state.
    trace_registers();

    try {
        for (;;) {
            after_call = false;
            after_return = false;

            bool done = cpu.step();

            // Show changed registers.
            trace_registers();

            if (progress_message_enabled) {
                show_progress();
            }
            simulated_instructions++;
            if (simulated_instructions > instr_limit)
                throw std::runtime_error("Simulation limit exceeded");

            if (done) {
                // Halted by 'stop' instruction.
                cpu.finish();
                return;
            }
        }

    } catch (const Processor::Exception &ex) {
        // Unexpected situation in the machine.
        auto pc = cpu.get_pc();
        cpu.stack_correction();
        cpu.finish();

        auto const *message = ex.what();
        if (!message[0]) {
            // Empty message - legally halted by extracode e74.
            return;
        }
        std::cerr << sim_progname << ": error: " << message << " @" << std::oct
                  << std::setfill('0') << std::setw(5) << pc << std::endl;
        throw std::runtime_error(message);

    } catch (std::exception &ex) {
        // Something else.
        cpu.finish();
        // std::cerr << "Error: " << ex.what() << std::endl;
        throw std::runtime_error(ex.what());
    }
}

//
// Save output files. Lifecycle hook, kept as an instance method.
//
// cppcheck-suppress functionStatic
void Machine::finish()
{
}

//
// Record the program's _exit() status.  The accumulator holds a 41-bit signed
// C int; the low byte becomes the host process return code.  When --status is
// enabled, print the full value as a 41-bit signed integer on stdout.
//
void Machine::set_exit_status(Word acc)
{
    program_exit_status = acc & 0xff;
    if (report_status_enabled) {
        int64_t v = acc & BITS41;
        if (acc & BIT41)
            v -= (int64_t)1 << 41;
        std::cout << v << std::endl;
    }
}

//
// Fetch instruction word.
//
Word Machine::mem_fetch(unsigned addr)
{
    if (addr == 0) {
        throw Processor::Exception("Jump to zero");
    }

    Word val = memory.load(addr);

    if (!cpu.on_right_instruction()) {
        trace_fetch(addr, val);
    }
    return val & BITS48;
}

//
// Write word to memory.
//
void Machine::mem_store(unsigned addr, Word val)
{
    addr &= BITS(15);
    if (addr == 0)
        return;
    if (addr == STACK_LIMIT)
        throw Processor::Exception("stack protection violation");

    memory.store(addr, val);
    trace_memory_write(addr, val);
}

//
// Read word from memory.
//
Word Machine::mem_load(unsigned addr)
{
    addr &= BITS(15);
    if (addr == 0)
        return 0;
    if (addr == STACK_LIMIT)
        throw Processor::Exception("stack protection violation");

    Word val = memory.load(addr);
    trace_memory_read(addr, val);

    return val & BITS48;
}

//
// Load a BESM-6 a.out executable into memory.
//
// The on-disk layout (see cross/besm6/b.out.h) is: an 8-word header, then the
// const, text and data segment images in that order.  The linker lays the
// segments out in the word address space starting at BADDR = HDRSZ/W = 8, in the
// same const/text/data/bss order (see cmd/ld/ld.c); for a pure NMAGIC file the
// data segment is page-aligned.  bss is zero and not stored in the file.
//
void Machine::load_program(const std::string &filename)
{
    // One BESM-6 word is 6 bytes on disk.
    const unsigned W = 6;

    // Words 0..7 correspond to the header and are left free.
    const unsigned BASE = HDRSZ / W;

    FILE *file = fopen(filename.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("Cannot open " + filename);
    }

    // Read and validate the header. word_t is 64-bit in the cross build, so the
    // magic comparisons are exact; cppcheck also explores the native 'besm6'
    // config where word_t is a 32-bit int, hence the suppressions.
    struct exec hdr;
    // cppcheck-suppress compareValueOutOfTypeRangeError
    if (!fgethdr(file, &hdr) || N_BADMAG(hdr)) {
        fclose(file);
        throw std::runtime_error("Not a BESM-6 a.out binary: " + filename);
    }

    const unsigned nconst = hdr.a_const / W;
    const unsigned ntext  = hdr.a_text / W;
    const unsigned ndata  = hdr.a_data / W;
    const unsigned nbss   = hdr.a_bss / W;

    // Segment origins in the word address space.
    const unsigned corigin = BASE;
    const unsigned torigin = corigin + nconst;
    unsigned dorigin       = torigin + ntext;
    // cppcheck-suppress compareValueOutOfTypeRangeError
    if (hdr.a_magic == NMAGIC) {
        // Pure text: data is page-aligned.
        dorigin = (dorigin + PAGE_NWORDS - 1) / PAGE_NWORDS * PAGE_NWORDS;
    }
    const unsigned borigin = dorigin + ndata;

    if (borigin + nbss > STACK_BASE) {
        // The image (and the heap that grows above it) must stay below the stack.
        fclose(file);
        throw std::runtime_error("Program does not fit in memory: " + filename);
    }

    cpu.reset();

    // Load const, text and data images.  The file cursor is already positioned
    // just past the header, at the start of the const segment.
    for (unsigned i = 0; i < nconst; i++)
        memory.store(corigin + i, fgetw(file) & BITS48);
    for (unsigned i = 0; i < ntext; i++)
        memory.store(torigin + i, fgetw(file) & BITS48);
    for (unsigned i = 0; i < ndata; i++)
        memory.store(dorigin + i, fgetw(file) & BITS48);

    // bss is zero-filled.
    for (unsigned i = 0; i < nbss; i++)
        memory.store(borigin + i, 0);

    fclose(file);

    // Entry point is an absolute word address.
    cpu.set_pc(hdr.a_entry);

    // Seed the stack pointer (r15) at the base of the reserved stack region.  A real
    // crt0 would set this; the stack grows towards higher addresses, up to the guard
    // word STACK_LIMIT (see doc/Besm6_Calling_Conventions.md).
    cpu.set_m(017, STACK_BASE);

    // The program break starts on the first page boundary past bss; break()/sbrk()
    // grow it upwards.
    program_break = (borigin + nbss + PAGE_NWORDS - 1) / PAGE_NWORDS * PAGE_NWORDS;
}

//
// Replace the running image with a new a.out (the exec() Unix syscall).
//
// Reload the executable, then lay the argument block down at the fixed base
// STACK_BASE = 070000, exactly as the kernel's exece() does (kernel/sys1.c):
//
//      070000  argc
//              argv[0] .. argv[argc-1]     char* fat pointers
//              0
//              envp[0] .. envp[ne-1]
//              0
//              the strings, byte-packed six to a word, contiguous
//              0                           the cursor rounded up to a word
//        r15 = the first free word above the block
//
// The block sits at the BASE of the stack and r15 starts above it, so the
// program's own stack growth can never walk back over its own arguments, and
// argc is always at absolute 070000 -- which is how a crt0 finds it with no
// register hand-off.  Every pointer slot strides by ONE word; only the strings
// are byte-granular, and they are NOT re-aligned between one and the next, so
// all but the first argv[i] carries a byte offset in its fat pointer.
//
// Nothing is handed over in a register.  The kernel's setregs() zeroes ACC and
// r1..r14 (regloc[0..14] in kernel/trap.c) and sets only r15 and the return
// address; load_program() has just called Processor::reset(), which zeroes the
// whole core, so matching the kernel here means leaving the registers alone.
// The program break is likewise load_program()'s business: the heap grows above
// the bss, far below this block.
//
void Machine::exec(const std::string &filename, const std::vector<std::string> &argv,
                   const std::vector<std::string> &envp)
{
    // The kernel's own ceiling on the arg list: NCARGS in include/sys/param.h.
    const unsigned MAX_ARG_BYTES = 5120;

    // Every string carries its NUL.
    auto packed_size = [](const std::vector<std::string> &vec) {
        return std::accumulate(vec.begin(), vec.end(), 0u, [](unsigned n, const std::string &s) {
            return n + (unsigned)s.size() + 1;
        });
    };
    const unsigned nbytes = packed_size(argv) + packed_size(envp);

    // argc, the two vectors, their two terminating nulls, then the strings.
    const unsigned na    = (unsigned)argv.size() + (unsigned)envp.size();
    const unsigned sbase = STACK_BASE + 1 + na + 2;

    // Both checks come BEFORE the image is replaced, so a rejected exec() returns
    // to a caller that still exists.  Memory::store is unchecked, so the second one
    // is what keeps an oversized vector off the stack guard and out of bounds.
    if (nbytes > MAX_ARG_BYTES)
        throw std::runtime_error("Argument list too long: " + filename);
    if (sbase + (nbytes + 5) / 6 + 1 >= STACK_LIMIT)
        throw std::runtime_error("Argument list does not fit in memory: " + filename);

    // Loads the image and seeds PC, the stack pointer and the break.
    load_program(filename);

    unsigned ap = STACK_BASE;
    BytePointer up(memory, sbase, 0);

    memory.store(ap, argv.size());
    for (const auto &vec : { &argv, &envp }) {
        for (const auto &s : *vec) {
            // argv[i] / envp[i] is the cursor itself, as a fat pointer: bit 48
            // set, the byte offset in bits 47..45 (field 5 names byte #0).
            ap++;
            memory.store(ap, BIT48 | ((Word)(5 - up.byte_index) << 44) | up.word_addr);
            for (char c : s)
                up.put_byte((uint8_t)c);
            up.put_byte(0);
        }
        // The vector's terminating null.
        ap++;
        memory.store(ap, 0);
    }

    // Round the cursor up: the closing word must not eat a partial string.
    unsigned ucp = up.word_addr + (up.byte_index != 0);
    memory.store(ucp, 0);
    cpu.set_m(017, ucp + 1);
}
