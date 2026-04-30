#include "vm/runtime/vm.h"

using namespace lpc::vm;

std::string Vm::ResolveString(const Value &v) const {
    if (v.IsObjRef()) {
        std::uintptr_t raw = v.AsObj();
        if (raw > 0 && raw < kFuncBase) {
            if (raw & 1) {
                std::uint32_t sidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (sidx < BoundChunk().sconst.size()) {
                    return BoundChunk().sconst[sidx];
                }
            } else {
                std::uint32_t hidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (hidx < string_heap_.size()) {
                    return string_heap_[hidx];
                }
            }
        }
    }
    if (v.Tag() == ValueTag::Int64) {
        return std::to_string(v.AsI64());
    }
    if (v.IsFloat64()) {
        return std::to_string(v.AsF64());
    }
    return "";
}

std::string_view Vm::ResolveStringView(const Value &v, std::string &buf) const {
    if (v.IsObjRef()) {
        std::uintptr_t raw = v.AsObj();
        if (raw > 0 && raw < kFuncBase) {
            if (raw & 1) {
                std::uint32_t sidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (sidx < BoundChunk().sconst.size()) {
                    return BoundChunk().sconst[sidx];
                }
            } else {
                std::uint32_t hidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (hidx < string_heap_.size()) {
                    return string_heap_[hidx];
                }
            }
        }
    }
    if (v.Tag() == ValueTag::Int64) {
        buf = std::to_string(v.AsI64());
        return buf;
    }
    if (v.IsFloat64()) {
        buf = std::to_string(v.AsF64());
        return buf;
    }
    return "";
}

std::string Vm::ResolveObjRefStringOnly(const Value &v) const {
    if (!v.IsObjRef()) return "";
    std::uintptr_t raw = v.AsObj();
    if (raw >= kFuncBase) return "";
    if (raw > 0 && (raw & 1)) {
        std::uint32_t sidx = static_cast<std::uint32_t>(raw >> 1) - 1;
        if (sidx < BoundChunk().sconst.size()) return BoundChunk().sconst[sidx];
    } else if (raw > 0) {
        std::uint32_t hidx = static_cast<std::uint32_t>(raw >> 1) - 1;
        if (hidx < string_heap_.size()) return string_heap_[hidx];
    }
    return "";
}

bool Vm::IsStringObjRefFull(const Value &v) const {
    return IsStringObjRef(v) && !ResolveObjRefStringOnly(v).empty();
}

Value Vm::GetArrayElement(const Value &arr, std::int64_t index) const {
    std::size_t arr_id = DecodeArrayId(arr);
    if (LPC_UNLIKELY(arr_id == 0 || arr_id > arrays_.size())) return Value::Nil();
    const auto &a = arrays_[arr_id - 1];
    if (index < 0 || static_cast<std::size_t>(index) >= a.Size()) return Value::Nil();
    return a.At(static_cast<std::size_t>(index));
}

Value Vm::GetMappingElement(const Value &map, const Value &key) const {
    std::size_t map_id = DecodeMappingId(map);
    if (LPC_UNLIKELY(map_id == 0 || map_id > mappings_.size())) return Value::Nil();
    const Value *found = mappings_[map_id - 1].Find(key);
    return found ? *found : Value::Nil();
}

Value Vm::GetObjectField(const Value &obj, const std::string &field) const {
    std::size_t obj_id = DecodeObjectId(obj);
    if (obj_id == 0 || obj_id > objects_.size()) {
        std::size_t inst_id = DecodeClassId(obj);
        if (inst_id == 0 || inst_id > class_fields_.size()) return Value::Nil();
        std::size_t inst_idx = inst_id - 1;
        const ClassInfo *ci = ResolveClassInfoFromHandle(obj);
        if (ci) {
            int fi = ci->FindField(field);
            if (fi >= 0) {
                const LpcClass &fields = class_fields_[inst_idx];
                if (static_cast<std::size_t>(fi) < fields.Size()) return fields.At(fi);
            }
        }
        return Value::Nil();
    }
    const_cast<Vm*>(this)->TryUpgradeObject(obj_id);
    const auto &lobj = objects_[obj_id - 1];
    if (lobj.destroyed) return Value::Nil();
    const Chunk *bp = lobj.blueprint ? lobj.blueprint : &BoundChunk();
    const Chunk *ver_chunk = GetChunkForVersion(lobj.module_version_id);
    if (ver_chunk) {
        bp = ver_chunk;
    }
    for (std::size_t i = 0; i < bp->global_names.size(); ++i) {
        if (bp->global_names[i] == field) {
            if (i < lobj.globals.size()) return lobj.globals[i];
            return Value::Nil();
        }
    }
    return Value::Nil();
}

std::size_t Vm::GetArraySize(const Value &arr) const {
    std::size_t arr_id = DecodeArrayId(arr);
    if (LPC_UNLIKELY(arr_id == 0 || arr_id > arrays_.size())) return 0;
    return arrays_[arr_id - 1].Size();
}

std::size_t Vm::GetMappingSize(const Value &map) const {
    std::size_t map_id = DecodeMappingId(map);
    if (LPC_UNLIKELY(map_id == 0 || map_id > mappings_.size())) return 0;
    return mappings_[map_id - 1].Size();
}

std::vector<std::pair<Value, Value>> Vm::GetMappingPairs(const Value &map) const {
    std::size_t map_id = DecodeMappingId(map);
    if (map_id == 0 || map_id > mappings_.size()) return {};
    return mappings_[map_id - 1].ToPairVector();
}

const std::vector<std::string> &Vm::GetObjectFieldNames(const Value &obj) const {
    static const std::vector<std::string> empty;
    if (const ClassInfo *ci = ResolveClassInfoFromHandle(obj)) {
        return ci->field_names;
    }
    std::size_t obj_id = DecodeObjectId(obj);
    if (obj_id > 0 && obj_id <= objects_.size()) {
        const_cast<Vm*>(this)->TryUpgradeObject(obj_id);
        const auto &lobj = objects_[obj_id - 1];
        const Chunk *bp = lobj.blueprint ? lobj.blueprint : &BoundChunk();
        const Chunk *ver_chunk = GetChunkForVersion(lobj.module_version_id);
        if (ver_chunk) {
            bp = ver_chunk;
        }
        return bp->global_names;
    }
    return empty;
}
