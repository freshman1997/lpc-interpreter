#ifndef LPC_VM_RUNTIME_CONFIG_H
#define LPC_VM_RUNTIME_CONFIG_H

#include <cstdint>

namespace lpc {
namespace vm {

static constexpr std::uint32_t kMaxCallFrames       = 4096;
static constexpr std::uint32_t kMaxStackSize        = 131072;
static constexpr std::size_t   kGcThresholdInit      = 1024;
static constexpr std::size_t   kGcThresholdPadding    = 256;
static constexpr std::size_t   kGcThresholdMultiplier = 2;
static constexpr std::size_t   kFormatBufSize         = 64;

static constexpr std::uint8_t kUpsetSubOpInc  = 21;
static constexpr std::uint8_t kUpsetSubOpDec  = 22;
static constexpr std::uint8_t kUpsetFlagPrefix = 1;

} // namespace vm
} // namespace lpc

#ifndef LPC_LIKELY
#ifdef __GNUC__
#define LPC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define LPC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LPC_LIKELY(x)   (x)
#define LPC_UNLIKELY(x) (x)
#endif
#endif

#ifndef LPC_FORCEINLINE
#ifdef _MSC_VER
#define LPC_FORCEINLINE __forceinline
#elif defined(__GNUC__)
#define LPC_FORCEINLINE inline __attribute__((always_inline))
#else
#define LPC_FORCEINLINE inline
#endif
#endif

#ifndef LPC_ENABLE_COMPUTED_GOTO
#if defined(__GNUC__) && !defined(_WIN32)
#define LPC_ENABLE_COMPUTED_GOTO 1
#else
#define LPC_ENABLE_COMPUTED_GOTO 0
#endif
#endif

#endif
