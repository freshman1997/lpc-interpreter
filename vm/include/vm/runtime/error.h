#ifndef LPC_VM_RUNTIME_ERROR_H
#define LPC_VM_RUNTIME_ERROR_H

#include <string>

namespace lpc {
namespace vm {

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
    UndefinedFunction,
    NotFound,
    ParseError,
};

struct RuntimeError {
    RuntimeErrorCode code = RuntimeErrorCode::None;
    std::string message;
    std::string module;
    std::string function;
    int line = 0;
    int pc = 0;

    bool ok() const { return code == RuntimeErrorCode::None; }

    static RuntimeError Ok() {
        return {};
    }

    static RuntimeError Error(RuntimeErrorCode c, const std::string &msg) {
        RuntimeError e;
        e.code = c;
        e.message = msg;
        return e;
    }
};

} // namespace vm
} // namespace lpc

#endif
