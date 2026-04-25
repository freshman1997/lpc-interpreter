#ifndef LPC_NEXTVM_RUNTIME_VM_H
#define LPC_NEXTVM_RUNTIME_VM_H

#include <vector>
#include <string>

#include "nextvm/value/value.h"
#include "nextvm/runtime/frame.h"
#include "nextvm/runtime/error.h"
#include "nextvm/bytecode/chunk.h"

namespace lpc {
namespace nextvm {

class Vm {
public:
    RuntimeError LoadChunk(const Chunk &chunk);
    RuntimeError RunEntry(const char *function_name);

    const std::vector<Frame> &frames() const { return frames_; }
    Value last_result() const { return last_result_; }

private:
    Chunk chunk_;
    std::vector<Value> value_stack_;
    std::vector<Frame> frames_;
    Value last_result_;
};

} // namespace nextvm
} // namespace lpc

#endif
