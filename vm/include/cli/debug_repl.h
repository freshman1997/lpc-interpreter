#ifndef LPC_CLI_DEBUG_REPL_H
#define LPC_CLI_DEBUG_REPL_H

#include <cstdint>
#include "vm/runtime/error.h"

namespace lpc {
namespace vm {

class Vm;

RuntimeError RunDebugReplStep(Vm &vm, std::uint32_t pc);

} // namespace vm
} // namespace lpc

#endif
