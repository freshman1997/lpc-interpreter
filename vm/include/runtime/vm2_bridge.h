#ifndef LPC_RUNTIME_VM2_BRIDGE_H
#define LPC_RUNTIME_VM2_BRIDGE_H

#include <string>

#include "core/status.h"

namespace lpc {
namespace runtime {

core::Status RunEntryModuleVm2(const std::string &entry_module);
core::Status RunEntryModuleCompare(const std::string &entry_module);

} // namespace runtime
} // namespace lpc

#endif
