#ifndef LPC_FRONTEND2_SOURCE_H
#define LPC_FRONTEND2_SOURCE_H

#include <string>

namespace lpc {
namespace frontend2 {

struct SourceSpan {
    int line = 1;
    int column = 1;
    int length = 0;
};

struct SourceFile {
    std::string path;
    std::string text;
};

} // namespace frontend2
} // namespace lpc

#endif
