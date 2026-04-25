#ifndef LPC_RUNTIME_bytecode_translator_H
#define LPC_RUNTIME_bytecode_translator_H

#include <string>

#include "core/status.h"

namespace lpc {
namespace runtime {

core::Status RunEntryModuleNextVM(const std::string &entry_module);
core::Status RunEntryModuleCompare(const std::string &entry_module);

} // namespace runtime
} // namespace lpc

#endif
