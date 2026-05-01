#ifndef LPC_CLI_DAP_SERVER_H
#define LPC_CLI_DAP_SERVER_H

#include <cstdint>
#include <string>
#include "vm/runtime/error.h"

namespace lpc {
namespace vm {

class Vm;

RuntimeError RunDapServerStep(Vm &vm, std::uint32_t pc);
void RunDapServer(Vm &vm, const std::string &entry_function = "main");
bool StartDapAttachServer(Vm &vm, int port);

} // namespace vm
} // namespace lpc

#endif
