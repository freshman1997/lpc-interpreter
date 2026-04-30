#ifndef LPC_VM_VALUE_OBJREF_H
#define LPC_VM_VALUE_OBJREF_H

#include <cstdint>
#include <cstddef>

#include "vm/value/value.h"

namespace lpc {
namespace vm {

static constexpr std::uintptr_t kFuncBase     = 0x080000000ULL;
static constexpr std::uintptr_t kArrayBase    = 0x100000000ULL;
static constexpr std::uintptr_t kMappingBase  = 0x200000000ULL;
static constexpr std::uintptr_t kClassBase    = 0x300000000ULL;
static constexpr std::uintptr_t kObjectBase   = 0x400000000ULL;

inline bool IsStringObjRef(const Value &v) {
    if (!v.IsObjRef()) return false;
    auto raw = v.AsObj();
    return raw > 0 && raw < kFuncBase;
}

inline bool IsFuncObjRef(const Value &v) {
    if (!v.IsObjRef()) return false;
    auto raw = v.AsObj();
    return raw >= kFuncBase && raw < kArrayBase;
}

inline bool IsArrayObjRef(const Value &v) {
    if (!v.IsObjRef()) return false;
    auto raw = v.AsObj();
    return raw >= kArrayBase && raw < kMappingBase;
}

inline bool IsMappingObjRef(const Value &v) {
    if (!v.IsObjRef()) return false;
    auto raw = v.AsObj();
    return raw >= kMappingBase && raw < kClassBase;
}

inline bool IsClassObjRef(const Value &v) {
    if (!v.IsObjRef()) return false;
    auto raw = v.AsObj();
    return raw >= kClassBase && raw < kObjectBase;
}

inline bool IsObjectObjRef(const Value &v) {
    if (!v.IsObjRef()) return false;
    auto raw = v.AsObj();
    return raw >= kObjectBase;
}

inline std::size_t DecodeFuncId(const Value &v) {
    if (!v.IsObjRef()) return 0;
    auto raw = v.AsObj();
    if (raw < kFuncBase || raw >= kArrayBase) return 0;
    return static_cast<std::size_t>(raw - kFuncBase);
}

inline Value MakeFuncHandle(std::size_t func_id) {
    return Value::FromObj(kFuncBase + func_id);
}

inline std::size_t DecodeArrayId(const Value &v) {
    if (!v.IsObjRef()) return 0;
    auto raw = v.AsObj();
    if (raw < kArrayBase || raw >= kMappingBase) return 0;
    return static_cast<std::size_t>(raw - kArrayBase);
}

inline Value MakeArrayHandle(std::size_t arr_id) {
    return Value::FromObj(kArrayBase + arr_id);
}

inline std::size_t DecodeMappingId(const Value &v) {
    if (!v.IsObjRef()) return 0;
    auto raw = v.AsObj();
    if (raw < kMappingBase || raw >= kClassBase) return 0;
    return static_cast<std::size_t>(raw - kMappingBase);
}

inline Value MakeMappingHandle(std::size_t map_id) {
    return Value::FromObj(kMappingBase + map_id);
}

inline std::size_t DecodeClassId(const Value &v) {
    if (!v.IsObjRef()) return 0;
    auto raw = v.AsObj();
    if (raw < kClassBase || raw >= kObjectBase) return 0;
    return static_cast<std::size_t>(raw - kClassBase);
}

inline Value MakeClassHandle(std::size_t cls_id) {
    return Value::FromObj(kClassBase + cls_id);
}

inline std::size_t DecodeObjectId(const Value &v) {
    if (!v.IsObjRef()) return 0;
    auto raw = v.AsObj();
    if (raw < kObjectBase) return 0;
    return static_cast<std::size_t>(raw - kObjectBase);
}

inline Value MakeObjectHandle(std::size_t obj_id) {
    return Value::FromObj(kObjectBase + obj_id);
}

inline Value MakeBool(bool b) {
    return Value::FromBool(b);
}

inline Value MakeI64(std::int64_t i) {
    if (i >= Value::kIntMinInline && i <= Value::kIntMaxInline)
        return Value::FromI64(i);
    return Value::FromI64(i);
}

inline Value MakeF64(double f) {
    return Value::FromF64(f);
}

} // namespace vm
} // namespace lpc

#endif
