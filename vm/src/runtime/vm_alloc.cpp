#include "vm/runtime/vm.h"

using namespace lpc::vm;

static Value MakeClassFieldDefault(Vm &vm, const ClassInfo::FieldDefault &def) {
    switch (def.kind) {
    case ClassInfo::FieldDefault::Kind::Int:
        return vm.MakeI64(def.int_value);
    case ClassInfo::FieldDefault::Kind::Float:
        return Value::FromF64(def.float_value);
    case ClassInfo::FieldDefault::Kind::String:
        return vm.InternString(def.string_value);
    case ClassInfo::FieldDefault::Kind::Mapping: {
        Mapping map;
        for (const auto &pair : def.mapping_pairs) {
            map.Insert(MakeClassFieldDefault(vm, pair.first), MakeClassFieldDefault(vm, pair.second));
        }
        return vm.AllocateMappingHandle(std::move(map));
    }
    case ClassInfo::FieldDefault::Kind::Array: {
        std::vector<Value> values;
        values.reserve(def.array_items.size());
        for (const auto &item : def.array_items) {
            values.push_back(MakeClassFieldDefault(vm, item));
        }
        return vm.AllocateArrayHandle(std::move(values));
    }
    case ClassInfo::FieldDefault::Kind::Zero:
    default:
        return vm.MakeI64(0);
    }
}

static LpcClass BuildDefaultClassFields(Vm &vm, const ClassInfo &ci) {
    LpcClass fields(ci.nfields, vm.MakeI64(0));
    for (std::size_t i = 0; i < ci.field_defaults.size() && i < fields.Size(); ++i) {
        fields.Set(i, MakeClassFieldDefault(vm, ci.field_defaults[i]));
    }
    return fields;
}

bool Vm::ResolveClassTemplateIndex(const Value &class_handle, std::uint16_t *out_template_idx) const {
    if (!out_template_idx) return false;
    std::size_t cls_id = DecodeClassId(class_handle);
    if (cls_id == 0 || cls_id > class_fields_.size()) return false;
    std::size_t inst_idx = cls_id - 1;
    if (inst_idx >= class_template_ids_.size()) return false;
    *out_template_idx = class_template_ids_[inst_idx];
    return true;
}

const ClassInfo *Vm::ResolveClassInfoFromHandle(const Value &class_handle) const {
    std::uint16_t tmpl_idx = 0;
    if (!ResolveClassTemplateIndex(class_handle, &tmpl_idx)) return nullptr;
    std::size_t cls_id = DecodeClassId(class_handle);
    if (cls_id > 0 && cls_id <= class_module_version_ids_.size()) {
        std::size_t inst_idx = cls_id - 1;
        std::uint64_t ver_id = class_module_version_ids_[inst_idx];
        const std::string &mod_name = class_module_names_[inst_idx];
        if (ver_id != 0 && !mod_name.empty()) {
            const ModuleRuntimeState *ms = GetModuleState(mod_name);
            if (ms) {
                auto it = ms->version_runtime_data.find(ver_id);
                if (it != ms->version_runtime_data.end() &&
                    tmpl_idx < it->second.chunk.classes.size()) {
                    return &it->second.chunk.classes[tmpl_idx];
                }
            }
        }
    }
    return &BoundChunk().classes[tmpl_idx];
}

Value Vm::AllocateArrayHandle(std::vector<Value> &&elements) {
    LpcArray arr;
    arr.InitFromVector(std::move(elements));
    return AllocateArrayHandle(std::move(arr));
}

Value Vm::AllocateArrayHandle(LpcArray &&array) {
    ++alloc_count_;
    if (!array_free_.empty()) {
        std::size_t idx = array_free_.back();
        array_free_.pop_back();
        arrays_[idx] = std::move(array);
        array_slot_free_[idx] = 0;
        return MakeArrayHandle(idx + 1);
    }
    arrays_.push_back(std::move(array));
    array_slot_free_.push_back(0);
    return MakeArrayHandle(arrays_.size());
}

Value Vm::AllocateMappingHandle(Mapping &&mapping) {
    ++alloc_count_;
    if (!mapping_free_.empty()) {
        std::size_t idx = mapping_free_.back();
        mapping_free_.pop_back();
        mappings_[idx] = std::move(mapping);
        mapping_slot_free_[idx] = 0;
        return MakeMappingHandle(idx + 1);
    }
    mappings_.push_back(std::move(mapping));
    mapping_slot_free_.push_back(0);
    return MakeMappingHandle(mappings_.size());
}

Value Vm::AllocateClassHandle(std::uint16_t class_idx) {
    ++alloc_count_;
    if (!class_free_.empty()) {
        std::size_t idx = class_free_.back();
        class_free_.pop_back();
        class_fields_[idx] = BuildDefaultClassFields(*this, BoundChunk().classes[class_idx]);
        class_template_ids_[idx] = class_idx;
        class_module_version_ids_[idx] = current_module_version_id_;
        class_module_names_[idx] = current_module_name_;
        class_slot_free_[idx] = 0;
        module_class_instances_[current_module_name_].push_back(idx);
        return MakeClassHandle(idx + 1);
    }
    class_fields_.push_back(BuildDefaultClassFields(*this, BoundChunk().classes[class_idx]));
    class_template_ids_.push_back(class_idx);
    class_module_version_ids_.push_back(current_module_version_id_);
    class_module_names_.push_back(current_module_name_);
    class_slot_free_.push_back(0);
    std::size_t idx = class_fields_.size() - 1;
    module_class_instances_[current_module_name_].push_back(idx);
    return MakeClassHandle(class_fields_.size());
}

Value Vm::AllocateClosureHandle(LpcClosure &&closure) {
    ++alloc_count_;
    if (!closure_free_.empty()) {
        std::size_t idx = closure_free_.back();
        closure_free_.pop_back();
        closures_[idx] = std::move(closure);
        closure_slot_free_[idx] = 0;
        return Value::FromClosure(static_cast<std::uint32_t>(idx + 1));
    }
    std::uint32_t idx = static_cast<std::uint32_t>(closures_.size());
    closures_.push_back(std::move(closure));
    closure_slot_free_.push_back(0);
    return Value::FromClosure(idx + 1);
}

Value Vm::AllocateObjectHandle(LpcObject &&object) {
    ++alloc_count_;
    if (!object_free_.empty()) {
        std::size_t idx = object_free_.back();
        object_free_.pop_back();
        objects_[idx] = std::move(object);
        object_slot_free_[idx] = 0;
        return MakeObjectHandle(idx + 1);
    }
    objects_.push_back(std::move(object));
    object_slot_free_.push_back(0);
    return MakeObjectHandle(objects_.size());
}
