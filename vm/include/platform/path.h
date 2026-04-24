#ifndef LPC_PLATFORM_PATH_H
#define LPC_PLATFORM_PATH_H

#include <string>

namespace lpc {
namespace platform {

std::string JoinPath(const std::string &left, const std::string &right);
std::string NormalizePath(const std::string &path);

} // namespace platform
} // namespace lpc

#endif
