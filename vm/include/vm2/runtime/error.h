#ifndef LPC_VM2_RUNTIME_ERROR_H
#define LPC_VM2_RUNTIME_ERROR_H

#include <string>

namespace lpc {
namespace vm2 {

enum class RuntimeErrorCode {
    None = 0,
    InvalidOpcode,
    StackUnderflow,
    InvalidOperand,
    TypeError,
    ArityMismatch,
    NotCallable,
    BoundsError,
    InternalError,
};

struct RuntimeError {
    RuntimeErrorCode code = RuntimeErrorCode::None;
    std::string message;
    std::string module;
    std::string function;
    int line = 0;
    int pc = 0;

    bool ok() const { return code == RuntimeErrorCode::None; }
};

} // namespace vm2
} // namespace lpc

#endif
