#ifndef LPC_VM_BYTECODE_VERIFIER_H
#define LPC_VM_BYTECODE_VERIFIER_H

#include "vm/runtime/error.h"
#include "vm/bytecode/chunk.h"

namespace lpc {
namespace vm {

RuntimeError VerifyChunk(const Chunk &chunk);

} // namespace vm
} // namespace lpc

#endif
