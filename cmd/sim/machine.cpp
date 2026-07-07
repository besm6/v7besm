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
    redirect_trace(nullptr, "");
    enable_trace("");
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
        std::cerr << "Error: " << message << " @" << std::oct << std::setfill('0') << std::setw(5)
                  << pc << std::endl;
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

    // The program break starts just past bss; break()/sbrk() grow it upwards.
    program_break = borigin + nbss;
}

//
// Replace the running image with a new a.out (the exec() Unix syscall).
//
// Reload the executable, then build the argument vector on the fresh stack in
// the C calling convention for main(argc, argv): the string bodies and the
// argv[] array (a NUL-terminated list of char* fat pointers) are laid down at
// the current stack top, argc is pushed onto the stack, and argv is left in the
// accumulator.  envp is laid down the same way just above argv; there is no
// established crt0/argv ABI in the toolchain yet, so this is the minimal
// reasonable convention and is deliberately confined to this one method.
//
void Machine::exec(const std::string &filename, const std::vector<std::string> &argv,
                   const std::vector<std::string> &envp)
{
    // Loads the image and seeds PC, the stack pointer and the break.
    load_program(filename);

    // Pack a NUL-terminated argument list starting at word address `top`,
    // returning the word address of the char*[] vector and advancing `top`.
    unsigned top = cpu.get_m(017);
    auto lay_vector = [&](const std::vector<std::string> &vec) -> unsigned {
        // Vector of fat pointers, one per string plus a terminating null.
        const unsigned vec_addr = top;
        top += vec.size() + 1;
        for (size_t i = 0; i < vec.size(); i++) {
            // Store the string body, remembering a fat pointer to byte #0.
            const unsigned str_addr = top;
            BytePointer bp(memory, str_addr, 0);
            for (char c : vec[i])
                bp.put_byte((uint8_t)c);
            bp.put_byte(0);
            top = bp.word_addr + (bp.byte_index != 0);
            // Fat pointer: bit 48 set, offset field 5 (byte #0), word address.
            const Word fatptr = BIT48 | (5ull << 44) | str_addr;
            memory.store(vec_addr + i, fatptr);
        }
        memory.store(vec_addr + vec.size(), 0);
        return vec_addr;
    };

    const unsigned argv_addr = lay_vector(argv);
    lay_vector(envp);

    // main(argc, argv): argc on the stack, argv in the accumulator.
    memory.store(top, argv.size());
    cpu.set_m(017, top + 1);
    cpu.set_acc(argv_addr);
    cpu.set_m(14, (unsigned)-2 & BITS(15)); // r14 = negative argument count (two args)
    program_break = top + 1;
}
