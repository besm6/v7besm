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
// Selection of the last stage to run is controlled by -E (stop after cpp),
// -S (stop after codegen, emit assembly) and -c (stop after as, emit object).
// With none of those, the objects are linked into an executable.
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
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

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
            fprintf(stderr, "cc: out of memory\n");
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
static bool opt_g;      // -g: request debug info (currently a no-op; see TODO.md)
static bool opt_O;      // -O: request optimization (currently a no-op; see TODO.md)
static bool opt_v;      // -v: echo each sub-command before running it
static char *outfile;   // -o NAME: explicit output name

static struct vec sources;   // input .c/.s files to compile
static struct vec objects;   // .o (and produced) files to link
static struct vec cppflags;  // -D/-I/-U pass-throughs for the preprocessor
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
        fprintf(stderr, "cc: out of memory\n");
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
    fprintf(stderr, "cc: ");
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
    char *incdir = besm6_include_dir();

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
// Run the assembler: b6as -o out in.  Returns 0 on success.
//
static int run_as(const char *in, const char *out)
{
    char *tool = find_tool("B6AS", "b6as");
    if (!tool) {
        error("cannot find b6as");
        return 1;
    }
    char *av[] = { tool, "-o", (char *)out, (char *)in, NULL };
    int rc = run(tool, av);
    free(tool);
    return rc;
}

//
// Compile one source file through the pipeline up to the stage selected by the
// -E/-S/-c flags.  A produced object file is appended to `objects` so a later
// link step can pick it up.  Returns 0 on success.
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

    // Code generation: .tac -> .s
    const char *asmfile =
        opt_S ? own(outfile ? strdup(outfile) : replace_suffix(src, "s")) : make_temp("s");
    if (run_pass("B6CODEGEN", "b6codegen", tacfile, asmfile) != 0)
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
// NOTE: linking is not fully wired up yet -- no startup object (crt0) is
// installed and the -lc search path is undefined, so this stage is expected to
// fail for now.  See TODO.md.  Returns 0 on success.
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
    vec_push(&av, "-o");
    vec_push(&av, outfile ? outfile : (char *)"a.out");
    for (size_t i = 0; i < objects.len; i++)
        vec_push(&av, objects.data[i]);
    vec_push(&av, "-lc");
    vec_push(&av, NULL);

    int rc = run(tool, av.data);
    vec_free(&av);
    free(tool);
    return rc;
}

static void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "    b6cc [options] file...\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -c              Compile and assemble, but do not link\n");
    fprintf(stderr, "    -S              Compile only; emit assembly (.s)\n");
    fprintf(stderr, "    -E              Preprocess only; write to output or .i\n");
    fprintf(stderr, "    -o file         Set output file name\n");
    fprintf(stderr, "    -O              Optimize (reserved; currently a no-op)\n");
    fprintf(stderr, "    -g              Emit debug info (reserved; currently a no-op)\n");
    fprintf(stderr, "    -v              Verbose: echo each sub-command\n");
    fprintf(stderr, "    -Dname[=val]    Predefine a preprocessor macro\n");
    fprintf(stderr, "    -Uname          Undefine a preprocessor macro\n");
    fprintf(stderr, "    -Ipath          Add a header search directory\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    atexit(cleanup);

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            vec_push(&sources, arg);
            continue;
        }
        switch (arg[1]) {
        case 'c':
            opt_c = true;
            break;
        case 'S':
            opt_S = true;
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
    return errflag ? 1 : 0;
}
