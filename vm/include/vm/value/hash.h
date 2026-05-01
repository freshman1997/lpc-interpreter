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

inline std::uint64_t Avalanche64(std::uint64_t h) {
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    return h;
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

    std::uint64_t h = bits ^ (static_cast<std::uint64_t>(key.Tag()) * 0x9e3779b97f4a7c15ULL);
    return Avalanche64(h);
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
