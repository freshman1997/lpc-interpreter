#ifndef LPC_VM2_VALUE_VALUE_H
#define LPC_VM2_VALUE_VALUE_H

#include <cstdint>

namespace lpc {
namespace vm2 {

enum class ValueTag : std::uint8_t {
    Nil = 0,
    Bool,
    Int64,
    Float64,
    ObjRef,
};

enum class ObjType : std::uint8_t {
    String = 0,
    Array,
    Map,
    Function,
    Closure,
    Module,
    Class,
    Instance,
    NativeFn,
};

struct ObjHeader {
    ObjType type = ObjType::String;
    std::uint8_t marked = 0;
    std::uint32_t identity_hash = 0;
};

struct Value {
    ValueTag tag = ValueTag::Nil;
    union {
        std::int64_t i64;
        double f64;
        void *obj;
    } as = {0};

    static Value Nil() {
        return {};
    }
};

} // namespace vm2
} // namespace lpc

#endif
