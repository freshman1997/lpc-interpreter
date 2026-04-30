#ifndef LPC_VM_VALUE_VALUE_H
#define LPC_VM_VALUE_VALUE_H

#include <cstdint>
#include <cstring>

namespace lpc {
namespace vm {

enum class ValueTag : std::uint8_t {
    Nil = 0,
    Bool,
    Int64,
    Float64,
    ObjRef,
    Closure,
    BoxedInt,
};

struct Value {
    std::uint64_t bits_ = kNilBits;

    static constexpr std::uint64_t kQNaNBits   = 0xFFF8000000000000ULL;
    static constexpr std::uint64_t kTagMask     = 0xFULL << 47;
    static constexpr std::uint64_t kPayloadMask = (1ULL << 47) - 1;
    static constexpr int           kTagShift    = 47;
    static constexpr std::uint64_t kCanonNaN    = 0x7FF8000000000000ULL;

    static constexpr std::uint64_t kTagNil      = 0;
    static constexpr std::uint64_t kTagBool     = 1;
    static constexpr std::uint64_t kTagInt64    = 2;
    static constexpr std::uint64_t kTagObjRef   = 3;
    static constexpr std::uint64_t kTagClosure  = 4;
    static constexpr std::uint64_t kTagBoxedInt = 5;

    static constexpr std::int64_t  kIntMaxInline = (1LL << 46) - 1;
    static constexpr std::int64_t  kIntMinInline = -(1LL << 46);

    static constexpr std::uint64_t kNilBits = kQNaNBits | (kTagNil << kTagShift);

    ValueTag Tag() const {
        if (!IsTagged()) return ValueTag::Float64;
        switch ((bits_ & kTagMask) >> kTagShift) {
        case kTagNil:      return ValueTag::Nil;
        case kTagBool:     return ValueTag::Bool;
        case kTagInt64:    return ValueTag::Int64;
        case kTagObjRef:   return ValueTag::ObjRef;
        case kTagClosure:  return ValueTag::Closure;
        case kTagBoxedInt: return ValueTag::BoxedInt;
        default:           return ValueTag::Nil;
        }
    }

    bool IsTagged() const {
        return (bits_ & kQNaNBits) == kQNaNBits;
    }

    bool IsNil() const { return bits_ == kNilBits; }
    bool IsBool() const { return Tag() == ValueTag::Bool; }
    bool IsInt64() const { auto t = Tag(); return t == ValueTag::Int64 || t == ValueTag::BoxedInt; }
    bool IsFloat64() const { return !IsTagged(); }
    bool IsObjRef() const { return Tag() == ValueTag::ObjRef; }
    bool IsClosure() const { return Tag() == ValueTag::Closure; }
    bool IsBoxedInt() const { return Tag() == ValueTag::BoxedInt; }

    bool AsBool() const { return (bits_ & kPayloadMask) != 0; }

    std::int64_t AsI64() const {
        std::uint64_t p = bits_ & kPayloadMask;
        if (p & (1ULL << 46)) p |= ~kPayloadMask;
        return static_cast<std::int64_t>(p);
    }

    double AsF64() const {
        double d;
        std::memcpy(&d, &bits_, sizeof(d));
        return d;
    }

    std::uintptr_t AsObj() const {
        return static_cast<std::uintptr_t>(bits_ & kPayloadMask);
    }

    std::uint32_t ClosureId() const {
        return static_cast<std::uint32_t>(bits_ & kPayloadMask);
    }

    std::size_t BoxedIntIdx() const {
        return static_cast<std::size_t>(bits_ & kPayloadMask);
    }

    std::uint64_t RawBits() const { return bits_; }

    static Value Nil() { return Value{kNilBits}; }

    static Value FromBool(bool b) {
        return Value{kQNaNBits | (kTagBool << kTagShift) | (b ? 1ULL : 0ULL)};
    }

    static Value FromI64(std::int64_t i) {
        std::uint64_t p = static_cast<std::uint64_t>(i) & kPayloadMask;
        return Value{kQNaNBits | (kTagInt64 << kTagShift) | p};
    }

    static Value FromF64(double f) {
        std::uint64_t b;
        std::memcpy(&b, &f, sizeof(b));
        if (((b >> 52) & 0x7FF) == 0x7FF && (b & 0x000FFFFFFFFFFFFFULL) != 0)
            b = kCanonNaN;
        return Value{b};
    }

    static Value FromObj(std::uintptr_t raw) {
        return Value{kQNaNBits | (kTagObjRef << kTagShift) |
                     (static_cast<std::uint64_t>(raw) & kPayloadMask)};
    }

    static Value FromClosure(std::uint32_t cid) {
        return Value{kQNaNBits | (kTagClosure << kTagShift) |
                     static_cast<std::uint64_t>(cid)};
    }

    static Value FromBoxedInt(std::size_t idx) {
        return Value{kQNaNBits | (kTagBoxedInt << kTagShift) |
                     static_cast<std::uint64_t>(idx)};
    }

    bool operator==(const Value &o) const { return bits_ == o.bits_; }
    bool operator!=(const Value &o) const { return bits_ != o.bits_; }
};

} // namespace vm
} // namespace lpc

#endif
