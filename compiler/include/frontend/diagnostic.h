#ifndef LPC_FRONTEND_DIAGNOSTIC_H
#define LPC_FRONTEND_DIAGNOSTIC_H

#include <string>
#include <vector>

#include "frontend/source.h"

namespace lpc {
namespace frontend {

enum class DiagnosticLevel {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    DiagnosticLevel level = DiagnosticLevel::Error;
    SourceSpan span;
    std::string message;
};

class DiagnosticSink {
public:
    void Add(DiagnosticLevel level, const SourceSpan &span, const std::string &message) {
        Diagnostic d;
        d.level = level;
        d.span = span;
        d.message = message;
        diagnostics_.push_back(d);
    }

    bool HasErrors() const {
        for (const auto &d : diagnostics_) {
            if (d.level == DiagnosticLevel::Error) {
                return true;
            }
        }
        return false;
    }

    const std::vector<Diagnostic> &All() const {
        return diagnostics_;
    }

private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace frontend
} // namespace lpc

#endif
