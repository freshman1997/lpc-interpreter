#ifndef LPC_FRONTEND2_VERIFIER2_H
#define LPC_FRONTEND2_VERIFIER2_H

#include <string>

#include "frontend2/mir.h"

namespace lpc {
namespace frontend2 {

struct Verify2Result {
    bool ok = true;
    std::string message;
    int function_index = -1;
    int instr_index = -1;
};

Verify2Result VerifyMirModule(const MirModule &module);

} // namespace frontend2
} // namespace lpc

#endif
