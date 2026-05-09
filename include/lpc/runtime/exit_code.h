#ifndef LPC_RUNTIME_EXIT_CODE_H
#define LPC_RUNTIME_EXIT_CODE_H

namespace lpc {
namespace runtime {

enum class ExitCode {
    Ok = 0,
    Usage = 2,
    RuntimeError = 10,
    InternalError = 11,
    IoError = 12,
    ProtocolError = 13,
};

inline int ToProcessCode(ExitCode code) {
    return static_cast<int>(code);
}

} // namespace runtime
} // namespace lpc

#endif
