#ifndef LPC_VM2_BYTECODE_VERIFIER_H
#define LPC_VM2_BYTECODE_VERIFIER_H

#include "vm2/runtime/error.h"
#include "vm2/bytecode/chunk.h"

namespace lpc {
namespace vm2 {

RuntimeError VerifyChunk(const Chunk &chunk);

} // namespace vm2
} // namespace lpc

#endif
