#ifndef LPC_FRONTEND_PREPROCESSOR_H
#define LPC_FRONTEND_PREPROCESSOR_H

#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/diagnostic.h"

namespace lpc {
namespace frontend {

struct SourceMapEntry {
    int output_line;
    int source_line;
    std::string source_path;
};

struct PreprocessResult {
    std::string text;
    std::unordered_map<std::string, std::string> defines;
    std::vector<SourceMapEntry> source_map;
};

PreprocessResult PreprocessSource(
    const std::string &path,
    const std::string &text,
    DiagnosticSink *diag,
    const std::vector<std::string> &include_dirs);

} // namespace frontend
} // namespace lpc

#endif
