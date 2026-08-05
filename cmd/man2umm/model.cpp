//
// The model's small shared parts: diagnostics, and the cross-reference rule.
//
#include "man2umm.h"

#include <ostream>

namespace umm {

void Diag::warn(const std::string &path, int line, const std::string &text)
{
    msgs.push_back({ false, line, path, text });
}

void Diag::error(const std::string &path, int line, const std::string &text)
{
    msgs.push_back({ true, line, path, text });
    nerr++;
}

void Diag::print(std::ostream &out) const
{
    for (const Message &m : msgs) {
        out << m.path;
        if (m.line > 0)
            out << ':' << m.line;
        out << ": " << (m.error ? "error: " : "warning: ") << m.text << '\n';
    }
}

} // namespace umm
