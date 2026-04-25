#ifndef LPC_FRONTEND_VERIFIER_H
#define LPC_FRONTEND_VERIFIER_H

#include <string>

#include "frontend/mir.h"

namespace lpc {
namespace frontend {

struct Verify2Result {
    bool ok = true;
    std::string message;
    int function_index = -1;
    int instr_index = -1;
};

Verify2Result VerifyMirModule(const MirModule &module);

} // namespace frontend
} // namespace lpc

#endif
