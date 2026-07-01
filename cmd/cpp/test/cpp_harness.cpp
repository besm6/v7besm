#include "cpp_harness.h"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

extern char** environ;

// Configure-time injected strings (see CMakeLists.txt).  Fall back to a
// conformant clang invocation if the build system did not define them.
#ifndef C11PP_COMMAND
#define C11PP_COMMAND "clang"
#endif
#ifndef C11PP_ARGS
#define C11PP_ARGS "-E,-std=c11"
#endif
#ifndef C11PP_STRICT_ARGS
#define C11PP_STRICT_ARGS "-Werror,-pedantic-errors"
#endif

namespace c11pp {
namespace {

// Split a comma-separated argument string, dropping empty fields.  (Comma, not
// ';', because ';' is CMake's list separator and would break the compile-time
// -D definitions.)
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, ',')) {
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int RemoveEntry(const char* path, const struct stat*, int, struct FTW*) {
    return remove(path);
}

void RemoveTree(const std::string& dir) {
    // Depth-first so directories are removed after their contents.
    nftw(dir.c_str(), RemoveEntry, 8, FTW_DEPTH | FTW_PHYS);
}

Result Run(const std::string& source,
           const std::vector<std::string>& extraArgs,
           const std::vector<AuxFile>& aux,
           bool strict) {
    // Unique scratch directory under $TMPDIR.
    const char* tmp = getenv("TMPDIR");
    std::string base = (tmp && *tmp) ? std::string(tmp) : std::string("/tmp");
    if (!base.empty() && base.back() == '/') base.pop_back();
    std::string tmpl = base + "/c11pp.XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data())) throw std::runtime_error("mkdtemp failed");
    std::string dir(buf.data());

    const std::string mainFile = dir + "/input.c";
    const std::string outFile = dir + "/stdout.txt";
    const std::string errFile = dir + "/stderr.txt";

    try {
        WriteFile(mainFile, source);
        for (const auto& a : aux) WriteFile(dir + "/" + a.name, a.content);

        // argv = command  base-args  [strict-args]  extra-args  -I<dir>  input.c
        std::vector<std::string> parts;
        parts.push_back(C11PP_COMMAND);
        const std::vector<std::string> baseArgs = Split(C11PP_ARGS);
        parts.insert(parts.end(), baseArgs.begin(), baseArgs.end());
        if (strict) {
            const std::vector<std::string> strictArgs = Split(C11PP_STRICT_ARGS);
            parts.insert(parts.end(), strictArgs.begin(), strictArgs.end());
        }
        parts.insert(parts.end(), extraArgs.begin(), extraArgs.end());
        parts.push_back("-I" + dir);
        parts.push_back(mainFile);

        std::vector<char*> argv;
        argv.reserve(parts.size() + 1);
        std::transform(
            parts.begin(), parts.end(), std::back_inserter(argv),
            [](const std::string& p) { return const_cast<char*>(p.c_str()); });
        argv.push_back(nullptr);

        // Redirect child stdout/stderr to files (no pipe-deadlock risk).
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, outFile.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, errFile.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);

        pid_t pid = 0;
        int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        if (rc != 0)
            throw std::runtime_error(std::string("cannot spawn '") + C11PP_COMMAND +
                                     "': " + std::strerror(rc));

        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }

        Result r;
        r.out = ReadFile(outFile);
        r.err = ReadFile(errFile);
        if (WIFEXITED(status))
            r.exit_code = WEXITSTATUS(status);
        else
            r.exit_code = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);

        RemoveTree(dir);
        return r;
    } catch (...) {
        RemoveTree(dir);
        throw;
    }
}

}  // namespace

Result Preprocess(const std::string& source,
                  const std::vector<std::string>& extraArgs,
                  const std::vector<AuxFile>& aux) {
    return Run(source, extraArgs, aux, /*strict=*/false);
}

Result PreprocessStrict(const std::string& source,
                        const std::vector<std::string>& extraArgs,
                        const std::vector<AuxFile>& aux) {
    return Run(source, extraArgs, aux, /*strict=*/true);
}

namespace {

bool IsWordChar(unsigned char c) { return std::isalnum(c) != 0 || c == '_'; }

// Is `line` a GNU line marker ("# 12 \"file\"") or a #line marker?  Such lines
// are preprocessor bookkeeping, not translated tokens, so they are dropped.
bool IsLineMarker(const std::string& line) {
    std::size_t p = line.find_first_not_of(" \t");
    if (p == std::string::npos || line[p] != '#') return false;
    std::size_t q = line.find_first_not_of(" \t", p + 1);
    if (q == std::string::npos) return false;
    return std::isdigit(static_cast<unsigned char>(line[q])) != 0 ||
           line.compare(q, 4, "line") == 0;
}

}  // namespace

std::string Normalize(const std::string& out) {
    // Pass 1: drop line-marker lines.
    std::string body;
    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line)) {
        if (!IsLineMarker(line)) {
            body += line;
            body.push_back('\n');
        }
    }

    // Pass 2: split into a canonical preprocessing-token sequence and re-join
    // with single spaces.  Only token identity/order is compared; incidental
    // spacing (e.g. a compiler's guard space in "2 +3") is not.  String and
    // character literals are kept intact (their interior spacing IS
    // significant); identifier/number runs stay whole so "a b" != "ab".
    std::vector<std::string> tokens;
    const std::size_t n = body.size();
    for (std::size_t i = 0; i < n;) {
        unsigned char c = body[i];
        if (std::isspace(c)) {
            ++i;
        } else if (c == '"' || c == '\'') {
            const char quote = static_cast<char>(c);
            std::string tok(1, body[i++]);
            while (i < n) {
                const char d = body[i++];
                tok.push_back(d);
                if (d == '\\' && i < n) {
                    tok.push_back(body[i++]);  // escaped char, keep verbatim
                } else if (d == quote) {
                    break;
                }
            }
            tokens.push_back(tok);
        } else if (IsWordChar(c)) {
            std::string tok;
            while (i < n && IsWordChar(static_cast<unsigned char>(body[i])))
                tok.push_back(body[i++]);
            tokens.push_back(tok);
        } else {
            // Maximal run of punctuation.  Splitting is applied identically to
            // expected and actual output, so exact punctuator boundaries need
            // not match the standard's — only consistency matters.
            std::string tok;
            while (i < n) {
                unsigned char d = body[i];
                if (std::isspace(d) || IsWordChar(d) || d == '"' || d == '\'')
                    break;
                tok.push_back(body[i++]);
            }
            tokens.push_back(tok);
        }
    }

    std::string result;
    for (std::size_t k = 0; k < tokens.size(); ++k) {
        if (k) result.push_back(' ');
        result += tokens[k];
    }
    return result;
}

}  // namespace c11pp
