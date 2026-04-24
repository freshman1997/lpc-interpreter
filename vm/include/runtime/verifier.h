#ifndef LPC_RUNTIME_VERIFIER_H
#define LPC_RUNTIME_VERIFIER_H

#include <string>

#include "type/lpc_proto.h"

namespace vm {

struct VerifyResult {
    bool ok = true;
    std::string message;
    lint32_t offset = -1;
};

VerifyResult VerifyV1Bytecode(const object_proto_t &proto);

} // namespace vm

#endif
