#ifndef LPC_CORE_STATUS_H
#define LPC_CORE_STATUS_H

#include <string>

namespace lpc {
namespace core {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    IoError,
    ParseError,
    CompileError,
    VmError,
    InternalError,
};

struct Status {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    bool ok() const { return code == ErrorCode::Ok; }

    static Status OkStatus() {
        return {};
    }

    static Status Error(ErrorCode c, const std::string &msg) {
        Status s;
        s.code = c;
        s.message = msg;
        return s;
    }
};

} // namespace core
} // namespace lpc

#endif
