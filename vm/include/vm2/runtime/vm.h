#ifndef LPC_VM2_RUNTIME_VM_H
#define LPC_VM2_RUNTIME_VM_H

#include <vector>
#include <string>

#include "vm2/value/value.h"
#include "vm2/runtime/frame.h"
#include "vm2/runtime/error.h"
#include "vm2/bytecode/chunk.h"

namespace lpc {
namespace vm2 {

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

} // namespace vm2
} // namespace lpc

#endif
