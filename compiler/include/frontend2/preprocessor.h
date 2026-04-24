#ifndef LPC_FRONTEND2_PREPROCESSOR_H
#define LPC_FRONTEND2_PREPROCESSOR_H

#include <string>
#include <unordered_map>
#include <vector>

#include "frontend2/diagnostic.h"

namespace lpc {
namespace frontend2 {

struct PreprocessResult {
    std::string text;
    std::unordered_map<std::string, std::string> defines;
};

PreprocessResult PreprocessSource(
    const std::string &path,
    const std::string &text,
    DiagnosticSink *diag,
    const std::vector<std::string> &include_dirs);

} // namespace frontend2
} // namespace lpc

#endif
