#ifndef LPC_VM_RUNTIME_FRAME_H
#define LPC_VM_RUNTIME_FRAME_H

#include <cstdint>
#include <string>

namespace lpc {
namespace vm {

struct Frame {
    std::uint32_t func_id = 0;
    std::uint32_t ip = 0;
    std::uint32_t base = 0;
    std::uint32_t stack_top = 0;
    std::uint32_t closure_slot = 0;
    std::uint32_t object_id = 0;
    std::uint64_t module_version_id = 0;
    bool version_pinned = false;
    std::string module_name;
};

} // namespace vm
} // namespace lpc

#endif
