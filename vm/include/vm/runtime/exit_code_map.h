#ifndef LPC_VM_RUNTIME_EXIT_CODE_MAP_H
#define LPC_VM_RUNTIME_EXIT_CODE_MAP_H

#include "lpc/runtime/exit_code.h"
#include "vm/runtime/error.h"

namespace lpc {
namespace vm {

inline lpc::runtime::ExitCode MapRuntimeErrorToExitCode(const RuntimeError &e) {
    using C = RuntimeErrorCode;
    if (e.code == C::None) {
        return lpc::runtime::ExitCode::Ok;
    }
    if (e.code == C::InternalError) {
        return lpc::runtime::ExitCode::InternalError;
    }
    if (e.code == C::ParseError || e.code == C::NotFound || e.code == C::ArityMismatch) {
        return lpc::runtime::ExitCode::Usage;
    }
    return lpc::runtime::ExitCode::RuntimeError;
}

inline int ProcessExitCodeFromRuntimeError(const RuntimeError &e) {
    return lpc::runtime::ToProcessCode(MapRuntimeErrorToExitCode(e));
}

} // namespace vm
} // namespace lpc

#endif
