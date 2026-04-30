#ifndef LPC_VM_RUNTIME_VM_H
#define LPC_VM_RUNTIME_VM_H

#include <vector>
#include <array>
#include <chrono>
#include <string>
#include <functional>
#include <unordered_map>
#include <ostream>

#include "vm/value/value.h"
#include "vm/value/objref.h"
#include "vm/value/mapping.h"
#include "vm/value/lpc_array.h"
#include "vm/value/lpc_class.h"
#include "vm/value/lpc_closure.h"
#include "vm/runtime/frame.h"
#include "vm/runtime/error.h"
#include "vm/runtime/debugger.h"
#include "vm/runtime/hot_reload.h"
#include "vm/runtime/vm_config.h"
#include "vm/bytecode/chunk.h"

namespace lpc {
namespace vm {

class Vm {
public:
    struct LpcObject {
        std::string module_name;
        std::uint64_t module_version_id = 0;
        const Chunk *blueprint = nullptr;
        std::vector<Value> globals;
        bool destroyed = false;
    };

    RuntimeError LoadChunk(const Chunk &chunk);
    RuntimeError LoadModule(const std::string &module_name, const Chunk &chunk);
    RuntimeError RunEntry(const char *function_name);
    RuntimeError PrepareHotReload(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  std::uint64_t *out_candidate_version,
                                  HotReloadCompatReport *out_report,
                                  const MigrationDescriptor *migration = nullptr);
    RuntimeError ActivateHotReload(const std::string &module_name,
                                   std::uint64_t candidate_version,
                                   std::uint64_t *out_previous_active = nullptr);
    RuntimeError RollbackHotReload(const std::string &module_name, std::uint64_t version_id);
    ModuleHotReloadStatus GetHotReloadStatus(const std::string &module_name) const;
    const Chunk *GetChunkForVersion(std::uint64_t module_version_id) const;
    bool TryUpgradeObject(std::size_t obj_id);
    void UpgradeClassInstancesForModule(const std::string &module_name, std::uint64_t new_version_id);
    AuditLog &audit_log() { return hot_reload_manager_.audit_log(); }
    const AuditLog &audit_log() const { return hot_reload_manager_.audit_log(); }

    const std::vector<Frame> &frames() const { return frames_; }
    const std::vector<Value> &stack() const { return value_stack_; }
    const Chunk &chunk() const { return bound_vrdata_ ? bound_vrdata_->chunk : empty_chunk_; }
    Value last_result() const { return last_result_; }
    Debugger &debugger() { return debugger_; }
    const Debugger &debugger() const { return debugger_; }

    using DebugHook = std::function<RuntimeError(std::uint32_t pc)>;
    void set_debug_hook(DebugHook hook) { debug_hook_ = std::move(hook); }

    void set_profile_enabled(bool enabled) { profile_enabled_ = enabled; }
    bool profile_enabled() const { return profile_enabled_; }
    void BeginProfile() {
        if (!profile_enabled_) return;
        instruction_count_ = 0;
        opcode_counts_.fill(0);
        profile_start_ = std::chrono::steady_clock::now();
        profile_end_ = profile_start_;
    }
    void EndProfile() {
        if (!profile_enabled_) return;
        profile_end_ = std::chrono::steady_clock::now();
    }
    void RecordOpcode(std::uint8_t op) {
        if (!profile_enabled_) return;
        ++instruction_count_;
        ++opcode_counts_[op];
    }
    void PrintProfile(std::ostream &os) const;

    void set_gc_threshold(std::size_t threshold) { gc_threshold_ = threshold; }
    std::size_t gc_threshold() const { return gc_threshold_; }
    void CollectGarbage();
    std::string ResolveString(const Value &v) const;
    std::string_view ResolveStringView(const Value &v, std::string &buf) const;

    Value GetArrayElement(const Value &arr, std::int64_t index) const;
    Value GetMappingElement(const Value &map, const Value &key) const;
    std::vector<std::pair<Value, Value>> GetMappingPairs(const Value &map) const;
    Value GetObjectField(const Value &obj, const std::string &field) const;
    std::size_t GetArraySize(const Value &arr) const;
    std::size_t GetMappingSize(const Value &map) const;
    const std::vector<std::string> &GetObjectFieldNames(const Value &obj) const;
    std::string ResolveObjRefStringOnly(const Value &v) const;
    bool IsStringObjRefFull(const Value &v) const;

    Value MakeI64(std::int64_t i) {
        if (i >= Value::kIntMinInline && i <= Value::kIntMaxInline)
            return Value::FromI64(i);
        std::size_t idx = boxed_ints_.size();
        boxed_ints_.push_back(i);
        return Value::FromBoxedInt(idx);
    }

    std::int64_t GetI64(const Value &v) const {
        if (v.Tag() == ValueTag::Int64) return v.AsI64();
        if (v.Tag() == ValueTag::BoxedInt) {
            auto idx = v.BoxedIntIdx();
            return idx < boxed_ints_.size() ? boxed_ints_[idx] : 0;
        }
        return 0;
    }

    Value InternString(const std::string &s) {
        if (s.empty()) return Value::Nil();
        auto it = string_intern_.find(s);
        if (it != string_intern_.end()) return it->second;
        ++alloc_count_;
        Value out;
        if (!string_free_.empty()) {
            std::size_t idx = string_free_.back();
            string_free_.pop_back();
            string_heap_[idx] = s;
            out = Value::FromObj((idx + 1) << 1);
        } else {
            std::uint32_t hidx = static_cast<std::uint32_t>(string_heap_.size());
            string_heap_.push_back(s);
            out = Value::FromObj((hidx + 1) << 1);
        }
        string_intern_[s] = out;
        return out;
    }

    LpcArray &arrays_at(std::size_t idx) { return arrays_[idx]; }
    Mapping &mappings_at(std::size_t idx) { return mappings_[idx]; }
    LpcClass &class_fields_at(std::size_t idx) { return class_fields_[idx]; }
    std::vector<std::string> &string_heap() { return string_heap_; }
    std::vector<LpcClosure> &closures() { return closures_; }
    std::vector<LpcObject> &objects() { return objects_; }
    const std::vector<LpcObject> &objects() const { return objects_; }
    std::uint32_t current_object_id() const { return current_object_id_; }
    std::size_t &alloc_count() { return alloc_count_; }
    std::vector<std::size_t> &array_free() { return array_free_; }
    std::vector<std::size_t> &string_free() { return string_free_; }
    std::vector<std::size_t> &object_free() { return object_free_; }

    Chunk &BoundChunk() { return bound_vrdata_->chunk; }
    const Chunk &BoundChunk() const { return bound_vrdata_->chunk; }
    std::vector<Value> &BoundIConst() { return bound_vrdata_->iconst_values; }
    const std::vector<Value> &BoundIConst() const { return bound_vrdata_->iconst_values; }
    std::vector<Value> &BoundFConst() { return bound_vrdata_->fconst_values; }
    const std::vector<Value> &BoundFConst() const { return bound_vrdata_->fconst_values; }
    std::vector<Value> &BoundSConst() { return bound_vrdata_->sconst_values; }
    const std::vector<Value> &BoundSConst() const { return bound_vrdata_->sconst_values; }
    int FindBoundFunction(const std::string &name) const { return bound_vrdata_->FindFunction(name); }
    int FindFunctionInModule(const std::string &module_name, std::uint64_t version_id, const std::string &func_name) const;
    int FindGlobalInModule(const std::string &module_name, std::uint64_t version_id, const std::string &global_name) const;

    Value AllocateArrayHandle(std::vector<Value> &&elements);
    Value AllocateMappingHandle(Mapping &&mapping);
    Value AllocateClassHandle(std::uint16_t class_idx);
    Value AllocateClosureHandle(LpcClosure &&closure);
    Value AllocateObjectHandle(LpcObject &&object);

private:
    static Chunk empty_chunk_;
    bool ResolveClassTemplateIndex(const Value &class_handle, std::uint16_t *out_template_idx) const;
    const ClassInfo *ResolveClassInfoFromHandle(const Value &class_handle) const;
    struct VersionRuntimeData {
        Chunk chunk;
        std::vector<Value> iconst_values;
        std::vector<Value> fconst_values;
        std::vector<Value> sconst_values;
        std::unordered_map<std::string, std::uint16_t> func_name_index;
        std::unordered_map<std::string, std::uint16_t> global_name_index;
        int FindFunction(const std::string &name) const {
            auto it = func_name_index.find(name);
            return it != func_name_index.end() ? static_cast<int>(it->second) : -1;
        }
        int FindGlobal(const std::string &name) const {
            auto it = global_name_index.find(name);
            return it != global_name_index.end() ? static_cast<int>(it->second) : -1;
        }
    };
    struct ModuleRuntimeState {
        std::string module_name;
        std::uint64_t active_version_id = 0;
        std::unordered_map<std::uint64_t, VersionRuntimeData> version_runtime_data;
        std::unordered_map<std::uint64_t, VersionRuntimeData> prepared_runtime_data;
    };
    RuntimeError BuildVersionRuntimeData(const Chunk &chunk, VersionRuntimeData *out_data);
    RuntimeError BindExecutionVersion(const std::string &module_name, std::uint64_t module_version_id);
    RuntimeError BindExecutionVersion(std::uint64_t module_version_id);
    RuntimeError RebindActiveChunkForCurrentModule();
    ModuleRuntimeState *GetModuleState(const std::string &module_name);
    const ModuleRuntimeState *GetModuleState(const std::string &module_name) const;

    // --- Hot path: accessed every opcode dispatch ---
    VersionRuntimeData *bound_vrdata_ = nullptr;
    std::vector<Value> value_stack_;
    std::vector<Frame> frames_;
    std::uint32_t current_object_id_ = 0;
    std::string current_module_name_;
    std::uint64_t current_module_version_id_ = 0;
    std::uint64_t bound_module_version_id_ = 0;
    std::string bound_module_name_;
    Value last_result_;
    std::unordered_map<std::string, ModuleRuntimeState> module_states_;
    std::unordered_map<std::uint64_t, VersionRuntimeData *> version_lookup_;

    // --- Warm path: accessed during allocation/efun ---
    std::vector<std::string> string_heap_;
    std::vector<LpcClosure> closures_;
    std::vector<LpcArray> arrays_;
    std::vector<Mapping> mappings_;
    std::vector<LpcClass> class_fields_;
    std::vector<std::uint16_t> class_template_ids_;
    std::vector<std::uint64_t> class_module_version_ids_;
    std::vector<std::string> class_module_names_;
    std::unordered_map<std::string, std::vector<std::size_t>> module_class_instances_;
    std::vector<LpcObject> objects_;
    std::vector<std::int64_t> boxed_ints_;

    // --- Cold path: GC marks, free lists, debug ---
    std::vector<uint8_t> array_marks_;
    std::vector<uint8_t> mapping_marks_;
    std::vector<uint8_t> class_marks_;
    std::vector<uint8_t> string_marks_;
    std::vector<uint8_t> closure_marks_;
    std::vector<uint8_t> object_marks_;
    std::vector<std::size_t> array_free_;
    std::vector<std::size_t> mapping_free_;
    std::vector<std::size_t> class_free_;
    std::vector<std::size_t> string_free_;
    std::vector<std::size_t> closure_free_;
    std::vector<std::size_t> object_free_;
    std::vector<uint8_t> array_slot_free_;
    std::vector<uint8_t> mapping_slot_free_;
    std::vector<uint8_t> class_slot_free_;
    std::vector<uint8_t> closure_slot_free_;
    std::vector<uint8_t> object_slot_free_;
    std::size_t alloc_count_ = 0;
    std::size_t gc_threshold_ = kGcThresholdInit;
    std::unordered_map<std::string, Value> string_intern_;
    bool profile_enabled_ = false;
    std::uint64_t instruction_count_ = 0;
    std::array<std::uint64_t, 256> opcode_counts_{};
    std::chrono::steady_clock::time_point profile_start_{};
    std::chrono::steady_clock::time_point profile_end_{};
    Debugger debugger_;
    DebugHook debug_hook_;
    HotReloadManager hot_reload_manager_;

    RuntimeError RunInitCode();
    Value DispatchIntrinsic(std::uint16_t efun_idx, std::uint8_t argc);
    void MarkReachable();
    void MarkValue(const Value &v);
    void Sweep();
};

} // namespace vm
} // namespace lpc

#endif
