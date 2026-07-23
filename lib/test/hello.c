//
// hello -- the smoke test for the lib/ build skeleton.
//
// It proves the whole chain in one run: crt0.s finds argc and argv at the fixed
// block the exec gate laid at 070000 and calls main() with them, the syscall leaves
// in sys/ reach b6sim, libc.a and crt0.o link ahead of the external c-compiler
// library, and main()'s status becomes the process's.
//
// It declares write() itself: v7 has no <unistd.h>, and include/ has none either.
// It measures its strings itself as well -- strlen belongs to phase 2, and taking
// one from the external library instead would be exactly the silent substitution
// README.md warns about.
//

int write(int fd, char *buf, int n);

// One string to the standard output, without stdio (phase 4).
static void put(char *s)
{
    char *p = s;
    int n   = 0;

    while (*p) {
        p++;
        n++;
    }
    write(1, s, n);
}

int main(int argc, char **argv, char **envp)
{
    int i;

    put("hello, world\n");

    // argv[i] is a fat char *; argv itself is a plain vector of words.
    for (i = 0; i < argc; i++) {
        put(argv[i]);
        put("\n");
    }
    return 0;
}
