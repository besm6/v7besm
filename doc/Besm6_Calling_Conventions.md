# Calling Conventions for C on BESM-6

## Registers

On BESM-6 we have fifteen general purpose registers named r1-15.
We use decimal numbering.

## On Call

 * The calling function pushes the arguments onto the stack in direct order.
 * The last argument passed remains in the accumulator.
 * Register r14 contains the number of passed arguments, *negative*.
 * Register r13 contains the return address to the calling function.

### Calling a `_Noreturn` function

A function declared `_Noreturn` (e.g. `exit`, `abort`, `longjmp`, or a user function)
never returns to its caller, so no return linkage is set up: the caller sets up the
arguments exactly as above but invokes the callee with a tail `uj` (unconditional jump)
instead of `call` (which is `13 vjm`, i.e. it loads r13 with the return address).
Because control never comes back, every instruction after the jump — including the
caller's own epilogue `uj b/ret` — is dead and is removed by the peephole pass. In the
TAC IR this is the dedicated `FunCallNoreturn` instruction; the front end emits it for a
*direct* call whose callee carries the `_Noreturn` flag.

### Defining a parameterless `_Noreturn` function

The standard prologue `its 13` / `call b/save0` exists to (1) save the return address
and the caller's `r5`/`r6`/`r7` so they can be restored on return, and (2) set up this
function's frame pointer `r7` (auto base) and the mode register `R = 7`. A function that is
itself declared `_Noreturn` **and takes no parameters** never returns, so job (1) is pure
waste — nothing is ever restored. The backend therefore drops the `b/save0` call (and the
dead `b/ret` epilogue) and inlines only what remains:

 * `ntr 7` — always, to establish the mode register `R = 7` that `b/save0` would have left.
 * `15 mtj 7` — only when the function has auto locals or compiler temporaries, to point
   `r7` at the incoming stack top before the `utm` reserves the auto slots. With no register
   save area pushed, the autos start directly at the caller's stack top.

The `_Noreturn`-ness of the definition is carried on the function's TAC top-level (`noret`,
serialized alongside `global`/`variadic`) so the machine backend can make this decision.
Functions with parameters still use `b/save` (its parameter-block setup is needed).

## On Return

 * The result value is returned in the accumulator.
 * Registers r1-r7 must be preserved by a called function.

### Returning a struct by value

A struct (or union) return value is classified by size:

 * **One machine word or smaller** (≤ 6 bytes): returned in the accumulator, exactly
   like a scalar.
 * **Larger than one word**: returned via a hidden pointer (the *sret* convention).
   The caller allocates the result storage and passes its address as an implicit
   **first argument**, shifting the declared parameters to slots 1, 2, …  The callee
   writes the whole struct through that pointer and also returns the pointer in the
   accumulator.  This lowering is performed entirely in the translator, so the machine
   backend never sees a struct-valued `RETURN` or a struct-valued call result.

 * Register r15 (stack pointer) has to be decremented by a called function
   by the number of arguments passed.
 * When the called function has 1 or more **parameters**, on return it should
   decrement r15 by the number of **passed arguments** minus 1.
 * When the called function has no **parameters**, it should return r15 unchanged.

## Example 1

Calling function with no arguments (and parameters). In C:

    flush()

In assembler:

    13 vjm flush

## Example 2

Calling function with one argument. In C:

    putch(a)

In assembler:

       xta a
    14 vtm -1
    13 vjm putch

## Example 3

Calling function with three arguments an a result. In C:

    result = foobar(a, b, c)

In assembler:

       xta a
       xts b
       xts c
    14 vtm -3
    13 vjm foobar
       atx result

# Stack Frame

The stack on BESM-6 grows towards increasing addresses.
In the pictures it means downwards.

Here is the stack layout at the moment of calling a function with N arguments.
Note that the last argument #N is located in the accumulator.

    ┌────────────────────┐
    │    argument #1     │◀── r15 initially (and after return)
    ├────────────────────┤
    │        ...         │
    ├────────────────────┤
    │    argument #N-1   │
    ├────────────────────┤
    │                    │◀── r15 after pushing arguments on stack

Here is the stack layout of the called function.

    ┌────────────────────┐
    │    argument #1     │◀── r6 as Parameter Pointer
    ├────────────────────┤
    │        ...         │
    ├────────────────────┤
    │    argument #N     │
    ├────────────────────┤
    │    saved r13       │
    ├────────────────────┤
    │    saved r7        │
    ├────────────────────┤
    │    saved r6        │
    ├────────────────────┤
    │  auto variable #1  │◀── r7 as Auto Pointer
    ├────────────────────┤
    │        ...         │
    ├────────────────────┤
    │  auto variable #N  │
    ├────────────────────┤
    │                    │◀── r15 as Stack Pointer


## Save Context

Every C function should start with:

       its 13
    13 vjm c/save

The c/save routine performs the following actions:

 * Save registers r13, r7 and r6 to the stack
 * Set r6 to point to the first argument
 * Set r7 to point to the first automatic variable (which will be allocated later)

Example:

    c/save:
         15 j+m 14
            its 7
            its 6
            its
         14 mtj 6
         15 mtj 7
         13 uj

## Restore Context

On return, every C function should perform:

        uj c/ret

The c/ret routine does the following:

 * Restore saved registers r13, r7 and r6
 * Restore stack pointer r15
 * Jump to the calling function

Example:

     c/ret:
          6 mtj 14
          7 mtj 15
          7 stx -4
            sti 6
            sti 7
            sti 13
         14 mtj 15
         13 uj
