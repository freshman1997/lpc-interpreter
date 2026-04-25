#ifndef LPC_NEXTVM_BYTECODE_VERIFIER_H
#define LPC_NEXTVM_BYTECODE_VERIFIER_H

#include "nextvm/runtime/error.h"
#include "nextvm/bytecode/chunk.h"

namespace lpc {
namespace nextvm {

RuntimeError VerifyChunk(const Chunk &chunk);

} // namespace nextvm
} // namespace lpc

#endif
