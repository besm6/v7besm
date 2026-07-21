//
// C compiler driver for the BESM-6.
//
// A host tool that turns C source into BESM-6 a.out objects/executables by
// driving the toolchain pipeline, one sub-tool per stage:
//
//     b6cpp   preprocess    .c  -> .i
//     b6parse parse         .i  -> .ast
//     b6lower lower + opt   .ast-> .tac
//     b6codegen code gen    .tac-> .s    (Madlen assembly)
//     b6as    assemble      .s  -> .o
//     b6ld    link          .o  -> a.out
//
// Input files are dispatched by suffix: .c runs the full pipeline, .S is
// preprocessed assembly (cpp -> as), .s is assembled directly, and .o is passed
// straight to the linker.
//
// Selection of the last stage to run is controlled by -E (stop after cpp),
// -S (stop after codegen, emit assembly) and -c (stop after as, emit object).
// With none of those, the objects are linked into an executable.  -Sbemsh and
// -Smadlen behave like -S but also select the b6codegen assembly dialect.
//
// This is a modern rewrite of the original Unix v7 cc(1) driver: the pipeline
// and tool names are new, and the argument handling, temp-file management and
// process spawning have been rewritten in C11.
//
#include <errno.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static char *progname = "cc"; // diagnostic prefix: basename of argv[0]

//
// A growable vector of C strings, used for argument lists and file lists.
// The stored pointers are borrowed unless noted; the vector owns only its
// backing array, freed with vec_free().
//
struct vec {
    char **data;
    size_t len;
    size_t cap;
};

static void vec_push(struct vec *v, char *s)
{
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = realloc(v->data, v->cap * sizeof(*v->data));
        if (!v->data) {
            fprintf(stderr, "%s: error: out of memory\n", progname);
            exit(1);
        }
    }
    v->data[v->len++] = s;
}

static void vec_free(struct vec *v)
{
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}

//
// Parsed command-line state.
//
static bool opt_c;      // -c: compile/assemble only, no link
static bool opt_S;      // -S: compile to assembly only
static bool opt_E;      // -E: preprocess only
static bool opt_g;      // -g: request debug info (a no-op; README.md, "Reserved options")
static bool opt_O;      // -O: request optimization (a no-op; README.md, "Reserved options")
static bool opt_v;         // -v: echo each sub-command before running it
static bool opt_nostdlib;  // -nostdlib: skip the library dirs, crt0.o and both -l's
static bool opt_nostdinc;  // -nostdinc: skip the standard system include dir
static char *outfile;      // -o NAME: explicit output name
static char *codegen_dialect;  // -Sbemsh/-Smadlen: dialect flag for b6codegen, or NULL

static struct vec sources;   // input .c/.s files to compile
static struct vec objects;   // .o (and produced) files to link
static struct vec cppflags;  // -D/-I/-U pass-throughs for the preprocessor
static struct vec ldflags;   // -L/-l pass-throughs for the linker
static struct vec tmpfiles;  // temp files to unlink on exit
static struct vec owned;     // heap-allocated file names to free on exit

static int errflag;  // set nonzero on any failure; becomes the exit status

//
// Take ownership of a heap-allocated string so it is freed at exit.  Every
// generated file name flows through here; the sources/objects/tmpfiles vectors
// only ever borrow these pointers.  Returns its argument for convenient
// chaining, e.g. own(replace_suffix(src, "o")).
//
static char *own(char *s)
{
    if (s)
        vec_push(&owned, s);
    return s;
}

//
// Return a newly allocated (and owned) concatenation of two strings.
//
static char *concat(const char *a, const char *b)
{
    size_t n = strlen(a) + strlen(b) + 1;
    char *s = malloc(n);
    if (!s) {
        fprintf(stderr, "%s: error: out of memory\n", progname);
        exit(1);
    }
    snprintf(s, n, "%s%s", a, b);
    return own(s);
}

//
// Report an error, printf-style, prefixed with "cc: ".  Records the failure
// but does not exit; the driver decides whether to keep going.
//
static void error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "%s: error: ", progname);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    errflag = 1;
}

//
// Remove every temp file we created.  Registered with atexit(), so it also
// runs on error paths that call exit().
//
static void cleanup(void)
{
    for (size_t i = 0; i < tmpfiles.len; i++)
        unlink(tmpfiles.data[i]);
    for (size_t i = 0; i < owned.len; i++)
        free(owned.data[i]);
    vec_free(&tmpfiles);
    vec_free(&owned);
}

//
// Return a pointer to the file-name suffix character after the final '.', or
// 0 if the name has no extension.  E.g. suffix_of("foo/bar.c") == 'c'.
//
static char suffix_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || strchr(dot, '/'))
        return 0;
    return dot[1] ? dot[1] : 0;
}

//
// Return a newly allocated copy of the base name of `name` (path stripped)
// with its suffix replaced by `.suf`.  Used to derive default output names,
// e.g. replace_suffix("src/foo.c", "o") == "foo.o".
//
static char *replace_suffix(const char *name, const char *suf)
{
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : strlen(base);

    char *out = malloc(stem + strlen(suf) + 2);
    if (!out) {
        error("out of memory");
        exit(1);
    }
    memcpy(out, base, stem);
    out[stem] = '.';
    strcpy(out + stem + 1, suf);
    return out;
}

//
// True if `a` and `b` name the same existing file (same device + inode).  Used
// to avoid clobbering a .S source on a case-insensitive filesystem, where the
// derived .s output resolves to the same file.  Returns false if either path
// cannot be stat()ed -- there is nothing to clobber yet.
//
static bool same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

//
// Create a temporary file whose name ends in `.suf`, register it for cleanup,
// and return its (heap-allocated) path.  Uses mkstemps(3) so the file is
// created atomically; the returned fd is closed immediately since the sub-tool
// reopens the path by name.
//
static char *make_temp(const char *suf)
{
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir)
        dir = "/tmp";

    size_t n = strlen(dir) + strlen(suf) + sizeof("/ccXXXXXX.");
    char *path = malloc(n);
    if (!path) {
        error("out of memory");
        exit(1);
    }
    snprintf(path, n, "%s/ccXXXXXX.%s", dir, suf);

    int fd = mkstemps(path, (int)strlen(suf) + 1);
    if (fd < 0) {
        error("cannot create temporary file: %s", strerror(errno));
        exit(1);
    }
    close(fd);
    vec_push(&tmpfiles, path);
    return own(path);
}

//
// Locate a sub-tool.  Resolution order:
//   1. the per-tool environment override, if set (e.g. B6CPP);
//   2. <dir>/<name> for dir in ~/.local/bin, then /usr/local/bin.
// Returns a heap-allocated path, or NULL if not found.
//
static char *find_tool(const char *envvar, const char *name)
{
    const char *override = getenv(envvar);
    if (override && *override)
        return strdup(override);

    const char *home = getenv("HOME");
    const char *dirs[3];
    int nd = 0;
    char localbin[1024];
    if (home && *home) {
        snprintf(localbin, sizeof(localbin), "%s/.local/bin", home);
        dirs[nd++] = localbin;
    }
    dirs[nd++] = "/usr/local/bin";

    for (int i = 0; i < nd; i++) {
        size_t n = strlen(dirs[i]) + strlen(name) + 2;
        char *path = malloc(n);
        if (!path) {
            error("out of memory");
            exit(1);
        }
        snprintf(path, n, "%s/%s", dirs[i], name);
        if (access(path, X_OK) == 0)
            return path;
        free(path);
    }
    return NULL;
}

//
// The three lookups below need no adjusting, and have needed none through two
// changes of who owns what.  The prefixes are fixed by convention -- ~/.local
// first, then /usr/local, share/besm6 under either -- and every producer
// installs there: the top-level CMake puts include/ in the first, lib/libc puts
// libc.a and crt0.o in the second, and the external c-compiler puts
// libruntime.a beside them.  A missing file here means a step of the bootstrap
// has not been run (see the README), never that the search order is wrong.
//

//
// Return the default besm6 include directory: <prefix>/share/besm6/include,
// where <prefix> is ~/.local if it exists, else /usr/local.  Heap-allocated,
// or NULL if it cannot be determined.
//
static char *besm6_include_dir(void)
{
    const char *home = getenv("HOME");
    if (home && *home) {
        size_t n = strlen(home) + sizeof("/.local/share/besm6/include");
        char *path = malloc(n);
        if (path) {
            snprintf(path, n, "%s/.local/share/besm6/include", home);
            if (access(path, X_OK) == 0)
                return path;
            free(path);
        }
    }
    char *fallback = strdup("/usr/local/share/besm6/include");
    if (fallback && access(fallback, X_OK) == 0)
        return fallback;
    free(fallback);
    return NULL;
}

//
// Locate the crt0 startup object.  Resolution order mirrors besm6_include_dir():
//   1. <HOME>/.local/share/besm6/lib/crt0.o, if it exists;
//   2. /usr/local/share/besm6/lib/crt0.o, if it exists.
// Returns a heap-allocated (and owned) full path, or NULL if neither is present.
//
static char *find_crt0(void)
{
    const char *home = getenv("HOME");
    if (home && *home) {
        char *path = concat(home, "/.local/share/besm6/lib/crt0.o");
        if (access(path, R_OK) == 0)
            return path;
    }
    char *fallback = concat("/usr/local/share/besm6/lib", "/crt0.o");
    if (access(fallback, R_OK) == 0)
        return fallback;
    return NULL;
}

//
// Append the standard BESM-6 library search directories to `av` as glued -L
// flags.  Unlike besm6_include_dir(), both prefixes are checked independently:
// <HOME>/.local/share/besm6/lib and /usr/local/share/besm6/lib are each added
// if they exist.  Suppressed by -nostdlib (the caller decides).
//
static void add_default_libdirs(struct vec *av)
{
    const char *home = getenv("HOME");
    if (home && *home) {
        char *dir = concat(home, "/.local/share/besm6/lib");
        if (access(dir, X_OK) == 0)
            vec_push(av, concat("-L", dir));
    }
    if (access("/usr/local/share/besm6/lib", X_OK) == 0)
        vec_push(av, own(strdup("-L/usr/local/share/besm6/lib")));
}

//
// Spawn `tool` with argument vector `argv` (NULL-terminated) and wait for it.
// Returns the child's exit status: 0 on success, nonzero on failure.  With
// -v, the command line is echoed first.
//
static int run(const char *tool, char *const argv[])
{
    if (opt_v) {
        for (char *const *p = argv; *p; p++)
            printf("%s ", *p);
        putchar('\n');
        fflush(stdout);
    }

    pid_t pid;
    int rc = posix_spawn(&pid, tool, NULL, NULL, argv, environ);
    if (rc != 0) {
        error("cannot run %s: %s", tool, strerror(rc));
        return 1;
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error("wait failed: %s", strerror(errno));
            return 1;
        }
    }
    if (WIFSIGNALED(status)) {
        error("%s killed by signal %d", tool, WTERMSIG(status));
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        error("%s exited with status %d", tool, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    return 0;
}

//
// Run the preprocessor: b6cpp [cppflags] -I<besm6-include> in out.
// Line markers are kept (b6parse understands them and maps diagnostics back to
// the original source).  Returns 0 on success.
//
static int run_cpp(const char *in, const char *out)
{
    char *tool = find_tool("B6CPP", "b6cpp");
    if (!tool) {
        error("cannot find b6cpp");
        return 1;
    }
    // The standard system include dir is added automatically unless -nostdinc.
    char *incdir = opt_nostdinc ? NULL : besm6_include_dir();

    struct vec av = { 0 };
    vec_push(&av, tool);
    for (size_t i = 0; i < cppflags.len; i++)
        vec_push(&av, cppflags.data[i]);
    // b6cpp wants the search directory glued to the flag: -Ipath, not -I path.
    if (incdir)
        vec_push(&av, concat("-I", incdir));
    vec_push(&av, (char *)in);
    vec_push(&av, (char *)out);
    vec_push(&av, NULL);

    int rc = run(tool, av.data);
    vec_free(&av);
    free(tool);
    free(incdir);
    return rc;
}

//
// Run a simple one-in/one-out compiler pass: <tool> in out.  `envvar` and
// `name` name the sub-tool for find_tool().  Returns 0 on success.
//
static int run_pass(const char *envvar, const char *name, const char *in, const char *out)
{
    char *tool = find_tool(envvar, name);
    if (!tool) {
        error("cannot find %s", name);
        return 1;
    }
    char *av[] = { tool, (char *)in, (char *)out, NULL };
    int rc = run(tool, av);
    free(tool);
    return rc;
}

//
// Run the code generator: b6codegen [--bemsh|--madlen] in out.  Unlike the
// other passes it may carry a dialect flag, selected by -Sbemsh/-Smadlen; for a
// plain -S the flag is omitted so b6codegen uses its own default.  Returns 0 on
// success.
//
static int run_codegen(const char *in, const char *out)
{
    char *tool = find_tool("B6CODEGEN", "b6codegen");
    if (!tool) {
        error("cannot find b6codegen");
        return 1;
    }
    struct vec av = { 0 };
    vec_push(&av, tool);
    if (codegen_dialect)
        vec_push(&av, codegen_dialect);
    vec_push(&av, (char *)in);
    vec_push(&av, (char *)out);
    vec_push(&av, NULL);
    int rc = run(tool, av.data);
    vec_free(&av);
    free(tool);
    return rc;
}

//
// Run the assembler: b6as -o out in.  Returns 0 on success.
//
static int run_as(const char *in, const char *out)
{
    char *tool = find_tool("B6AS", "b6as");
    if (!tool) {
        error("cannot find b6as");
        return 1;
    }
    char *av[] = { tool, "-X", "-o", (char *)out, (char *)in, NULL };
    int rc = run(tool, av);
    free(tool);
    return rc;
}

//
// Compile one source file through the pipeline up to the stage selected by the
// -E/-S/-c flags.  A .c file runs the full pipeline; a .S file is preprocessed
// assembly (cpp -> as); a .s file only needs assembling.  A produced object file
// is appended to `objects` so a later link step can pick it up.  Returns 0 on
// success.
//
static int compile_one(const char *src)
{
    char suf = suffix_of(src);

    // A .s file only needs assembling; a .o file is already an object.
    if (suf == 's') {
        if (opt_E || opt_S)
            return 0;
        char *obj = own(outfile && opt_c ? strdup(outfile) : replace_suffix(src, "o"));
        int rc = run_as(src, obj);
        vec_push(&objects, obj);
        return rc;
    }

    // A .S file is assembly that must be preprocessed first: cpp -> as.  b6as
    // treats the resulting "# line" markers as comments, so run_cpp's output
    // feeds straight into the assembler.
    if (suf == 'S') {
        // Where the preprocessed assembly goes depends on the stop stage.
        const char *sfile;
        if (opt_E)
            sfile = own(outfile ? strdup(outfile) : replace_suffix(src, "i"));
        else if (opt_S)
            sfile = own(outfile ? strdup(outfile) : replace_suffix(src, "s"));
        else
            sfile = make_temp("s");

        // Guard against overwriting the source on a case-insensitive filesystem,
        // where replace_suffix("foo.S", "s") == "foo.s" names the same file.
        if ((opt_E || opt_S) && same_file(src, sfile)) {
            error("%s: refusing to overwrite input; use -o", src);
            return 1;
        }

        if (run_cpp(src, sfile) != 0)
            return 1;
        if (opt_E || opt_S)
            return 0;

        // Assemble the preprocessed output: .s -> .o
        char *obj = own(outfile && opt_c ? strdup(outfile) : replace_suffix(src, "o"));
        int rc = run_as(sfile, obj);
        vec_push(&objects, obj);
        return rc;
    }

    if (suf != 'c') {
        error("don't know how to compile %s", src);
        return 1;
    }

    // Preprocess: .c -> .i
    const char *ifile;
    if (opt_E)
        ifile = own(outfile ? strdup(outfile) : replace_suffix(src, "i"));
    else
        ifile = make_temp("i");
    if (run_cpp(src, ifile) != 0)
        return 1;
    if (opt_E)
        return 0;

    // Parse and lower: .i -> .ast -> .tac
    const char *astfile = make_temp("ast");
    if (run_pass("B6PARSE", "b6parse", ifile, astfile) != 0)
        return 1;
    const char *tacfile = make_temp("tac");
    if (run_pass("B6LOWER", "b6lower", astfile, tacfile) != 0)
        return 1;

    // Code generation: .tac -> .s (or .madlen/.bemsh for the -Smadlen/-Sbemsh
    // dialects, so the derived name reflects the assembly dialect emitted).
    const char *asmsuf = codegen_dialect ? codegen_dialect + 2 : "s";
    const char *asmfile =
        opt_S ? own(outfile ? strdup(outfile) : replace_suffix(src, asmsuf)) : make_temp("s");
    if (run_codegen(tacfile, asmfile) != 0)
        return 1;
    if (opt_S)
        return 0;

    // Assemble: .s -> .o
    char *obj = own(outfile && opt_c ? strdup(outfile) : replace_suffix(src, "o"));
    int rc = run_as(asmfile, obj);
    vec_push(&objects, obj);
    return rc;
}

//
// Link all collected objects into an executable via b6ld.
//
// Unless -nostdlib is given, the crt0 startup object (which provides the
// _start entry point) is located in the standard library directories and
// linked first; a missing crt0.o is a fatal error.  Two implicit archives
// close the line, ours then the compiler's.  See README.md, "Linking".
// Returns 0 on success.
//
static int link_objects(void)
{
    char *tool = find_tool("B6LD", "b6ld");
    if (!tool) {
        error("cannot find b6ld");
        return 1;
    }

    struct vec av = { 0 };
    vec_push(&av, tool);
    vec_push(&av, "-X");
    vec_push(&av, "-e");
    vec_push(&av, "_start");
    vec_push(&av, "-o");
    vec_push(&av, outfile ? outfile : (char *)"a.out");
    // Standard library search dirs come before the objects; -nostdlib skips them.
    if (!opt_nostdlib)
        add_default_libdirs(&av);
    // The crt0 startup object leads the link so its _start comes first; skipped
    // by -nostdlib, fatal if it cannot be found.
    if (!opt_nostdlib) {
        char *crt0 = find_crt0();
        if (!crt0) {
            // Almost always the bootstrap, not a wish for a freestanding link:
            // crt0.o is built and installed by lib/libc, so it is absent until
            // `make -C lib install' has run at least once.
            error("crt0.o not found in the standard library directories "
                  "(~/.local or /usr/local share/besm6/lib); run `make -C lib && "
                  "make -C lib install' to build and install the C library, or "
                  "use -nostdlib to link without it");
            vec_free(&av);
            free(tool);
            return 1;
        }
        vec_push(&av, crt0);
    }
    for (size_t i = 0; i < objects.len; i++)
        vec_push(&av, objects.data[i]);
    // User -L/-l flags follow the objects so user-named libraries resolve their
    // references (conventional link order).
    for (size_t i = 0; i < ldflags.len; i++)
        vec_push(&av, ldflags.data[i]);
    // The implicit C library, then the compiler's helper archive, unless
    // -nostdlib asked for a freestanding link.  libruntime.a is LAST, and the
    // order is not cosmetic: b6ld scans an archive once, in order, and libc
    // calls the b$* helpers while no helper calls back into libc.
    if (!opt_nostdlib) {
        vec_push(&av, "-lc");
        vec_push(&av, "-lruntime");
    }
    vec_push(&av, NULL);

    int rc = run(tool, av.data);
    vec_free(&av);
    free(tool);
    return rc;
}

static void usage(void)
{
    printf("Usage:\n");
    printf("    %s [options] file...\n", progname);
    printf("Options:\n");
    printf("    -c              Compile and assemble, but do not link\n");
    printf("    -S              Compile only; emit assembly (.s)\n");
    printf("    -Sbemsh         Like -S, but emit Bemsh-dialect assembly\n");
    printf("    -Smadlen        Like -S, but emit Madlen-dialect assembly\n");
    printf("    -E              Preprocess only; write to output or .i\n");
    printf("    -o file         Set output file name\n");
    printf("    -O              Optimize (reserved; currently a no-op)\n");
    printf("    -g              Emit debug info (reserved; currently a no-op)\n");
    printf("    -v              Verbose: echo each sub-command\n");
    printf("    -Dname[=val]    Predefine a preprocessor macro\n");
    printf("    -Uname          Undefine a preprocessor macro\n");
    printf("    -Ipath          Add a header search directory\n");
    printf("    -Lpath          Add a library search directory (for the linker)\n");
    printf("    -lname          Link against library libname (for the linker)\n");
    printf("    -nostdlib       Do not use the standard library dirs, crt0.o, -lc or -lruntime\n");
    printf("    -nostdinc       Do not add the standard system include directory\n");
    printf("Inputs are dispatched by suffix: .c (compile), "
           ".S (preprocess + assemble), .s (assemble), .o (link).\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    // Derive the diagnostic prefix from argv[0]'s basename (fallback "cc").
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    atexit(cleanup);

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            vec_push(&sources, arg);
            continue;
        }
        // Multi-character options that the single-letter switch would misread.
        if (strcmp(arg, "-nostdlib") == 0) {
            opt_nostdlib = true;
            continue;
        }
        if (strcmp(arg, "-nostdinc") == 0) {
            opt_nostdinc = true;
            continue;
        }
        switch (arg[1]) {
        case 'c':
            opt_c = true;
            break;
        case 'S':
            opt_S = true;
            if (arg[2] == '\0')
                break; // plain -S: codegen default dialect
            if (strcmp(arg + 2, "bemsh") == 0)
                codegen_dialect = "--bemsh";
            else if (strcmp(arg + 2, "madlen") == 0)
                codegen_dialect = "--madlen";
            else {
                error("unknown option %s", arg);
                usage();
            }
            break;
        case 'E':
            opt_E = true;
            break;
        case 'O':
            opt_O = true;
            break;
        case 'g':
            opt_g = true;
            break;
        case 'v':
            opt_v = true;
            break;
        case 'o':
            if (arg[2]) {
                outfile = arg + 2; // -ofile
            } else if (i + 1 < argc) {
                outfile = argv[++i]; // -o file
            } else {
                error("-o requires a file name");
                usage();
            }
            break;
        case 'D':
        case 'U':
        case 'I':
            // Pass through to the preprocessor.  b6cpp requires the value glued
            // to the flag (-DNAME), so fold the separated form (-D NAME) into
            // one token.
            if (arg[2]) {
                vec_push(&cppflags, arg);
            } else if (i + 1 < argc) {
                const char flag[3] = { '-', arg[1], '\0' };
                vec_push(&cppflags, concat(flag, argv[++i]));
            } else {
                error("%s requires an argument", arg);
                usage();
            }
            break;
        case 'L':
        case 'l':
            // Pass through to the linker.  Fold the separated form (-L dir) into
            // one glued token, matching the -D/-I handling above.
            if (arg[2]) {
                vec_push(&ldflags, arg);
            } else if (i + 1 < argc) {
                const char flag[3] = { '-', arg[1], '\0' };
                vec_push(&ldflags, concat(flag, argv[++i]));
            } else {
                error("%s requires an argument", arg);
                usage();
            }
            break;
        default:
            error("unknown option %s", arg);
            usage();
        }
    }

    if (sources.len == 0) {
        error("no input files");
        usage();
    }
    if ((opt_E || opt_S || opt_c) && outfile && sources.len > 1) {
        error("cannot specify -o with -c, -S or -E and multiple input files");
        return 1;
    }

    // Compile each source; a failure on one file skips the link step but does
    // not abort the remaining compilations.
    bool compiled_ok = true;
    for (size_t i = 0; i < sources.len; i++) {
        if (suffix_of(sources.data[i]) == 'o') {
            vec_push(&objects, sources.data[i]);
            continue;
        }
        if (compile_one(sources.data[i]) != 0)
            compiled_ok = false;
    }

    // Link unless we stopped early or a compile failed.
    if (compiled_ok && !opt_c && !opt_S && !opt_E && objects.len > 0)
        link_objects();

    vec_free(&sources);
    vec_free(&objects);
    vec_free(&cppflags);
    vec_free(&ldflags);
    return errflag ? 1 : 0;
}
