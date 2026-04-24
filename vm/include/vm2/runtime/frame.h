#ifndef LPC_VM2_RUNTIME_FRAME_H
#define LPC_VM2_RUNTIME_FRAME_H

#include <cstdint>

namespace lpc {
namespace vm2 {

struct Frame {
    std::uint32_t func_id = 0;
    std::uint32_t ip = 0;
    std::uint32_t base = 0;
    std::uint32_t stack_top = 0;
    std::uint32_t closure_slot = 0;
};

} // namespace vm2
} // namespace lpc

#endif
