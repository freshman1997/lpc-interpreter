#ifndef LPC_NEXTVM_RUNTIME_ERROR_H
#define LPC_NEXTVM_RUNTIME_ERROR_H

#include <string>

namespace lpc {
namespace nextvm {

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

} // namespace nextvm
} // namespace lpc

#endif
