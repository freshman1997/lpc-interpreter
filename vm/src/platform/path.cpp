#include "platform/path.h"

namespace lpc {
namespace platform {

std::string JoinPath(const std::string &left, const std::string &right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    const char last = left[left.size() - 1];
    if (last == '/' || last == '\\') {
        return left + right;
    }
    return left + "/" + right;
}

std::string NormalizePath(const std::string &path) {
    std::string out = path;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') {
            out[i] = '/';
        }
    }
    return out;
}

} // namespace platform
} // namespace lpc
