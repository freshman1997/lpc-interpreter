#ifndef LPC_VM_VALUE_HASH_H
#define LPC_VM_VALUE_HASH_H

#include <cstdint>
#include <cstring>

#include "vm/value/value.h"

namespace lpc {
namespace vm {

inline std::uint64_t _WyRead64(const std::uint8_t *p) {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

inline std::uint64_t _WyMix(std::uint64_t a, std::uint64_t b) {
    std::uint64_t r = a ^ 0x53c5ca59UL;
    r *= b ^ 0x74743c1bUL;
    r ^= r >> 32;
    r *= 0x6b59e3cbUL;
    r ^= r >> 28;
    return r;
}

inline std::uint64_t Wyhash64(std::uint64_t key) {
    std::uint64_t seed = 0x53c5ca59UL;
    const std::uint8_t *p = reinterpret_cast<const std::uint8_t *>(&key);
    std::uint64_t a = _WyRead64(p) ^ 0x53c5ca59UL;
    std::uint64_t b = seed ^ 0x74743c1bUL;
    return _WyMix(a, b);
}

inline std::uint64_t HashValue(const Value &key) {
    std::uint64_t bits = 0;
    switch (key.Tag()) {
    case ValueTag::Nil: bits = 0; break;
    case ValueTag::Bool:
    case ValueTag::Int64:
        std::memcpy(&bits, &key.bits_, sizeof(bits));
        break;
    case ValueTag::Float64: {
        double f = key.AsF64();
        if (f != f) {
            bits = 0x7FF8000000000000ULL;
        } else {
            std::memcpy(&bits, &f, sizeof(bits));
        }
        break;
    }
    case ValueTag::ObjRef:
        bits = key.AsObj() >> 3;
        break;
    case ValueTag::Closure:
        bits = key.ClosureId() >> 3;
        break;
    case ValueTag::BoxedInt:
        bits = key.BoxedIntIdx();
        break;
    }

    std::uint64_t h = Wyhash64(bits);
    h ^= static_cast<std::uint64_t>(key.Tag());
    h = Wyhash64(h);
    return h;
}

inline bool KeyEqual(const Value &a, const Value &b) {
    return a.bits_ == b.bits_;
}

inline bool ValuesEqual(const Value &a, const Value &b) {
    return a.bits_ == b.bits_;
}

inline bool IsTruthy(const Value &v) {
    switch (v.Tag()) {
    case ValueTag::Nil: return false;
    case ValueTag::Bool: return v.AsBool();
    case ValueTag::Int64: return v.AsI64() != 0;
    case ValueTag::Float64: return v.AsF64() != 0.0;
    case ValueTag::ObjRef: return v.AsObj() != 0;
    case ValueTag::Closure: return true;
    case ValueTag::BoxedInt: return true;
    }
    return false;
}

} // namespace vm
} // namespace lpc

#endif
