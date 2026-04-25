#ifndef LPC_FRONTEND_SOURCE_H
#define LPC_FRONTEND_SOURCE_H

#include <string>

namespace lpc {
namespace frontend {

struct SourceSpan {
    int line = 1;
    int column = 1;
    int length = 0;
};

struct SourceFile {
    std::string path;
    std::string text;
};

} // namespace frontend
} // namespace lpc

#endif
