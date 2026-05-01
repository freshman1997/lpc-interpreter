#include "vm/runtime/vm.h"
#include "vm/value/objref.h"
#include "vm/value/mapping.h"
#include "vm/value/lpc_array.h"
#include "vm/value/lpc_class.h"
#include "vm/value/lpc_closure.h"
#include "vm/runtime/vm_config.h"

using namespace lpc::vm;

void Vm::MarkValue(const Value &v) {
    if (v.IsObjRef()) {
        std::uintptr_t raw = v.AsObj();
        if (raw > 0 && raw < kFuncBase) {
            if ((raw & 1) == 0) {
                std::uint32_t hidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (hidx < string_marks_.size() && string_marks_[hidx] == 0) {
                    string_marks_[hidx] = 1;
                }
            }
        } else if (raw >= kArrayBase && raw < kMappingBase) {
            std::size_t arr_id = static_cast<std::size_t>(raw - kArrayBase);
            if (arr_id > 0 && arr_id <= arrays_.size()) {
                if (array_marks_[arr_id - 1] == 0) {
                    array_marks_[arr_id - 1] = 1;
                    const LpcArray &arr = arrays_[arr_id - 1];
                    for (std::size_t i = 0; i < arr.Size(); ++i) {
                        MarkValue(arr.At(i));
                    }
                }
            }
        } else if (raw >= kMappingBase && raw < kClassBase) {
            std::size_t map_id = static_cast<std::size_t>(raw - kMappingBase);
            if (map_id > 0 && map_id <= mappings_.size() && mapping_marks_[map_id - 1] == 0) {
                mapping_marks_[map_id - 1] = 1;
                mappings_[map_id - 1].ForEachEntry([this](const Value &k, const Value &v) {
                    MarkValue(k);
                    MarkValue(v);
                });
            }
        } else if (raw >= kClassBase && raw < kObjectBase) {
            std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            if (cls_id > 0 && cls_id <= class_fields_.size() && class_marks_[cls_id - 1] == 0) {
                class_marks_[cls_id - 1] = 1;
                const LpcClass &cls = class_fields_[cls_id - 1];
                for (std::size_t i = 0; i < cls.Size(); ++i) {
                    MarkValue(cls.At(i));
                }
            }
        } else if (raw >= kObjectBase) {
            std::size_t obj_id = static_cast<std::size_t>(raw - kObjectBase);
            if (obj_id > 0 && obj_id <= objects_.size() && object_marks_[obj_id - 1] == 0) {
                object_marks_[obj_id - 1] = 1;
                for (auto &g : objects_[obj_id - 1].globals) {
                    MarkValue(g);
                }
            }
        }
    } else if (v.IsClosure()) {
        std::size_t cid = static_cast<std::size_t>(v.ClosureId()) - 1;
        if (cid < closures_.size() && closure_marks_[cid] == 0) {
            closure_marks_[cid] = 1;
            const LpcClosure &cl = closures_[cid];
            for (std::size_t i = 0; i < cl.UpvalueCount(); ++i) {
                MarkValue(cl.GetUpvalue(i));
            }
        }
    } else if (v.IsBoxedInt()) {
        std::size_t idx = v.BoxedIntIdx();
        if (idx < boxed_int_marks_.size()) {
            boxed_int_marks_[idx] = 1;
        }
    }
}

void Vm::MarkReachable() {
    array_marks_.assign(arrays_.size(), 0);
    mapping_marks_.assign(mappings_.size(), 0);
    class_marks_.assign(class_fields_.size(), 0);
    string_marks_.assign(string_heap_.size(), 0);
    closure_marks_.assign(closures_.size(), 0);
    object_marks_.assign(objects_.size(), 0);
    boxed_int_marks_.assign(boxed_ints_.size(), 0);

    for (auto &v : value_stack_) {
        MarkValue(v);
    }
    for (auto &frame : frames_) {
        if (frame.closure_slot < closures_.size()) {
            closure_marks_[frame.closure_slot] = 1;
            const LpcClosure &cl = closures_[frame.closure_slot];
            for (std::size_t i = 0; i < cl.UpvalueCount(); ++i) {
                MarkValue(cl.GetUpvalue(i));
            }
        }
        if (frame.object_id > 0 && frame.object_id <= objects_.size()) {
            object_marks_[frame.object_id - 1] = 1;
            for (auto &g : objects_[frame.object_id - 1].globals) {
                MarkValue(g);
            }
        }
    }
    MarkValue(last_result_);
}

void Vm::Sweep() {
    for (std::size_t i = 0; i < arrays_.size(); ++i) {
        if (array_marks_[i] == 0 && (i >= array_slot_free_.size() || array_slot_free_[i] == 0)) {
            arrays_[i].Clear();
            array_free_.push_back(i);
            if (i < array_slot_free_.size()) array_slot_free_[i] = 1;
        }
    }
    for (std::size_t i = 0; i < mappings_.size(); ++i) {
        if (mapping_marks_[i] == 0 && (i >= mapping_slot_free_.size() || mapping_slot_free_[i] == 0)) {
            mappings_[i].Clear();
            mapping_free_.push_back(i);
            if (i < mapping_slot_free_.size()) mapping_slot_free_[i] = 1;
        }
    }
    for (std::size_t i = 0; i < class_fields_.size(); ++i) {
        if (class_marks_[i] == 0 && (i >= class_slot_free_.size() || class_slot_free_[i] == 0)) {
            class_fields_[i].Clear();
            if (i < class_template_ids_.size()) class_template_ids_[i] = 0xFFFF;
            class_free_.push_back(i);
            if (i < class_slot_free_.size()) class_slot_free_[i] = 1;
        }
    }
    for (std::size_t i = 0; i < string_heap_.size(); ++i) {
        if (string_marks_[i] == 0 && !string_heap_[i].empty()) {
            string_intern_.erase(string_heap_[i]);
            string_heap_[i].clear();
            string_free_.push_back(i);
        }
    }
    for (std::size_t i = 0; i < closures_.size(); ++i) {
        if (closure_marks_[i] == 0 && (i >= closure_slot_free_.size() || closure_slot_free_[i] == 0)) {
            closures_[i].Clear();
            closure_free_.push_back(i);
            if (i < closure_slot_free_.size()) closure_slot_free_[i] = 1;
        }
    }
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        if (object_marks_[i] == 0 && (i >= object_slot_free_.size() || object_slot_free_[i] == 0)) {
            objects_[i].globals.clear();
            objects_[i].module_name.clear();
            objects_[i].blueprint = nullptr;
            objects_[i].destroyed = true;
            object_free_.push_back(i);
            if (i < object_slot_free_.size()) object_slot_free_[i] = 1;
        }
    }
    for (std::size_t i = 0; i < boxed_ints_.size(); ++i) {
        if (boxed_int_marks_[i] == 0 && (i >= boxed_int_slot_free_.size() || boxed_int_slot_free_[i] == 0)) {
            boxed_ints_[i] = 0;
            boxed_int_free_.push_back(i);
            if (i < boxed_int_slot_free_.size()) boxed_int_slot_free_[i] = 1;
        }
    }
}

void Vm::CollectGarbage() {
    MarkReachable();
    Sweep();
    alloc_count_ = 0;
}
