//
// exece(char *name, char **argv, char **envp) -- replace this process image, and its
// environment with it.  Spelled `execve' in later Unixes; this is v7's name for it, and
// the one sysent[59] carries.
//
// Hand-written for the reason exec.s is: a successful exece never returns, so reaching
// the instruction after the extracode already means the call failed and the branch to
// cerror is unconditional.  See exec.s for the full note.
//
        .text
        .globl  exece, cerror

exece:
        $77 59                  // SYS_exece -- returns only on failure
        uj      cerror
