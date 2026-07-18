//
// BESM-6 processor unit.
//
#ifndef DUBNA_PROCESSOR_H
#define DUBNA_PROCESSOR_H

#include <cstdint>
#include <string>

#include "besm6_arch.h"

class Machine;
class Memory;
class MantissaExponent;

//
// Internal state of the processor.
//
struct CoreState {
    unsigned PC;           // program counter (SchAS)
    Word ACC;              // accumulator
    Word RMR;              // low-order bits register
    unsigned M[16];        // registers modifiers
    unsigned MOD;          // MOD register
    unsigned RAU;          // ALU mode (rezhim AU)
    bool right_instr_flag; // execute right half of the word (PrK)
    bool apply_mod_reg;    // modify address by register M[16] (PrIK)

    // Check and modify ALU mode.
    bool is_additive() const { return RAU & RAU_ADD; }
    bool is_multiplicative() const { return (RAU & (RAU_ADD | RAU_MULT)) == RAU_MULT; }
    bool is_logical() const { return (RAU & RAU_MODE) == RAU_LOG; }

    void set_additive()
    {
        RAU &= ~RAU_MODE;
        RAU |= RAU_ADD;
    }

    void set_multiplicative()
    {
        RAU &= ~RAU_MODE;
        RAU |= RAU_MULT;
    }

    void set_logical()
    {
        RAU &= ~RAU_MODE;
        RAU |= RAU_LOG;
    }
};

//
// BESM-6 processor.
//
class Processor {
private:
    // Reference to the machine.
    Machine &machine;

    // 32K words of virtual memory.
    Memory &memory;

    // Current state.
    struct CoreState core{};

    // Previous state, for tracing.
    struct CoreState prev{};

    unsigned RK{};    // instruction register
    unsigned Aex{};   // executive address
    int corr_stack{}; // stack correction on exception

    // Messages for arithmetic exceptions (thrown from arithmetic.cpp).
    const std::string MSG_ARITH_OVERFLOW = "Arithmetic overflow";
    const std::string MSG_ARITH_DIVZERO  = "Division by zero";

    // Dispatch a Unix v7 system call (see kernel/sysent.c).  Reached from the
    // extracode 077 trap in step(); the syscall number arrives in M[14].
    void syscall(unsigned num);

    //
    // Syscall helpers (syscall.cpp).
    //
    // Fetch the k-th argument (1-based) of a call passing `count` arguments:
    // the last one is in the accumulator, the rest sit below the stack pointer.
    Word syscall_arg(unsigned k, unsigned count);

    // Read a NUL-terminated string addressed by a char* fat pointer.
    std::string mem_get_string(Word fatptr);

    // Copy `n` bytes between host memory and a char* fat pointer.
    void mem_get_bytes(Word fatptr, char *dst, unsigned n);
    void mem_put_bytes(Word fatptr, const char *src, unsigned n);

    // Finish a syscall: result in the accumulator, errno (0 on success) in M[14].
    void sys_ok(int64_t result);
    // ... and a second result in r12, for v7's two-value calls (pipe, wait,
    // getpid, getuid, getgid).  See R_VAL2 in include/sys/reg.h.
    void sys_ok2(int64_t v1, int64_t v2);
    void sys_err(int host_errno);
    void sys_ret(int64_t result); // -1 -> sys_err(errno), else sys_ok(result)

    // Process-control syscalls, factored out for readability.
    void sys_exec(unsigned count, bool with_env);

    // Print executive address of extracode (tracing).
    void print_executive_address();

public:
    // Exception for unexpected situations.
    class Exception : public std::exception {
    private:
        std::string message;

    public:
        explicit Exception(const std::string &m) : message(m) {}
        const char *what() const noexcept override { return message.c_str(); }
    };

    // Constructor.
    Processor(Machine &machine, Memory &memory);

    // Reset to initial state.
    void reset();

    // Simulate one instruction.
    // Return true when the processor is stopped.
    bool step();

    // Stack correction in case of exception.
    void stack_correction();

    // Finalize the processor.
    void finish();

    // Set register value.
    void set_pc(unsigned val) { core.PC = val; }
    void set_m(unsigned index, unsigned val) { core.M[index] = val; }
    void set_rau(unsigned val) { core.RAU = val; }
    void set_acc(Word val) { core.ACC = val; }

    // Get register value.
    unsigned get_pc() const { return core.PC; }
    unsigned get_m(unsigned index) const { return core.M[index]; }
    unsigned get_rau() const { return core.RAU; }
    Word get_acc() const { return core.ACC; }
    Word get_rmr() const { return core.RMR; }
    bool on_right_instruction() const { return core.right_instr_flag; }

    // Arithmetics.
    void arith_add(Word val, bool negate_acc, bool negate_val);
    void arith_normalize_and_round(MantissaExponent acc, Word mr, bool round_flag);
    void arith_add_exponent(int val);
    void arith_change_sign(bool negate_acc);
    void arith_multiply(Word val);
    void arith_divide(Word val);
    void arith_shift(int nbits);

    // Print trace info.
    void print_instruction();
    void print_instruction_dispak();
    void print_registers();
};

#endif // DUBNA_PROCESSOR_H
