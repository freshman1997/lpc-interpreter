#include "vm/runtime/vm.h"
#include "vm/runtime/hot_reload.h"
#include "lpc/bytecode/opcode.h"

#include <vector>

using namespace lpc::vm;

static Value MakeClassFieldDefaultValue(Vm &vm, const ClassInfo::FieldDefault &def) {
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
            map.Insert(
                MakeClassFieldDefaultValue(vm, pair.first),
                MakeClassFieldDefaultValue(vm, pair.second));
        }
        return vm.AllocateMappingHandle(std::move(map));
    }
    case ClassInfo::FieldDefault::Kind::Array: {
        std::vector<Value> values;
        values.reserve(def.array_items.size());
        for (const auto &item : def.array_items) {
            values.push_back(MakeClassFieldDefaultValue(vm, item));
        }
        return vm.AllocateArrayHandle(std::move(values));
    }
    case ClassInfo::FieldDefault::Kind::Zero:
    default:
        return vm.MakeI64(0);
    }
}

static Value DefaultForClassField(Vm &vm, const ClassInfo &ci, std::size_t field_idx) {
    if (field_idx < ci.field_defaults.size()) {
        return MakeClassFieldDefaultValue(vm, ci.field_defaults[field_idx]);
    }
    return vm.MakeI64(0);
}

Chunk Vm::empty_chunk_;

RuntimeError Vm::LoadChunk(const Chunk &chunk) {
    if (current_module_version_id_ != 0) {
        return RuntimeError::Error(
            RuntimeErrorCode::InvalidOperand,
            "implicit reload is disabled; use PrepareHotReload/ActivateHotReload explicitly");
    }

    current_module_name_ = chunk.module_name.empty() ? "__anonymous__" : chunk.module_name;
    HotReloadCompatReport compat;
    std::uint64_t prepared_version = 0;
    RuntimeError prepare_err = hot_reload_manager_.PrepareHotReload(
        current_module_name_, chunk, HotReloadLevel::L0, &prepared_version, &compat);
    if (!prepare_err.ok()) {
        return prepare_err;
    }

    std::uint64_t previous_active = 0;
    RuntimeError activate_err = hot_reload_manager_.ActivatePrepared(current_module_name_, prepared_version, &previous_active);
    if (!activate_err.ok()) {
        return activate_err;
    }

    ModuleRuntimeState &ms = module_states_[current_module_name_];
    ms.module_name = current_module_name_;
    ms.active_version_id = prepared_version;

    VersionRuntimeData data;
    RuntimeError data_err = BuildVersionRuntimeData(chunk, &data);
    if (!data_err.ok()) {
        return data_err;
    }
    ms.version_runtime_data[prepared_version] = std::move(data);
    version_lookup_[prepared_version] = &ms.version_runtime_data[prepared_version];
    ms.prepared_runtime_data.clear();

    current_module_version_id_ = prepared_version;
    bound_module_version_id_ = prepared_version;
    bound_module_name_ = current_module_name_;
    RuntimeError bind_err = BindExecutionVersion(current_module_name_, prepared_version);
    if (!bind_err.ok()) {
        return bind_err;
    }

    value_stack_.clear();
    frames_.clear();
    last_result_ = Value::Nil();
    string_heap_.clear();
    closures_.clear();
    arrays_.clear();
    mappings_.clear();
    class_fields_.clear();
    class_template_ids_.clear();
    objects_.clear();
    object_marks_.clear();
    array_marks_.clear();
    mapping_marks_.clear();
    class_marks_.clear();
    string_marks_.clear();
    closure_marks_.clear();
    array_free_.clear();
    mapping_free_.clear();
    class_free_.clear();
    string_free_.clear();
    closure_free_.clear();
    object_free_.clear();
    array_slot_free_.clear();
    mapping_slot_free_.clear();
    class_slot_free_.clear();
    closure_slot_free_.clear();
    object_slot_free_.clear();
    boxed_ints_.clear();
    string_intern_.clear();
    module_class_instances_.clear();
    for (std::size_t i = 0; i < BoundChunk().sconst.size(); ++i) {
        if (!BoundChunk().sconst[i].empty()) {
            Value sv = Value::FromObj((i + 1) << 1 | 1);
            string_intern_[BoundChunk().sconst[i]] = sv;
        }
    }
    alloc_count_ = 0;
    current_object_id_ = 0;
    return {};
}

RuntimeError Vm::LoadModule(const std::string &module_name, const Chunk &chunk) {
    std::string mname = module_name.empty() ? "__anonymous__" : module_name;

    if (module_states_.count(mname)) {
        return RuntimeError::Error(
            RuntimeErrorCode::InvalidOperand,
            "module already loaded; use PrepareHotReload/ActivateHotReload for updates");
    }

    HotReloadCompatReport compat;
    std::uint64_t prepared_version = 0;
    RuntimeError prepare_err = hot_reload_manager_.PrepareHotReload(
        mname, chunk, HotReloadLevel::L0, &prepared_version, &compat);
    if (!prepare_err.ok()) {
        return prepare_err;
    }

    std::uint64_t previous_active = 0;
    RuntimeError activate_err = hot_reload_manager_.ActivatePrepared(mname, prepared_version, &previous_active);
    if (!activate_err.ok()) {
        return activate_err;
    }

    ModuleRuntimeState &ms = module_states_[mname];
    ms.module_name = mname;
    ms.active_version_id = prepared_version;

    VersionRuntimeData data;
    RuntimeError data_err = BuildVersionRuntimeData(chunk, &data);
    if (!data_err.ok()) {
        return data_err;
    }
    ms.version_runtime_data[prepared_version] = std::move(data);
    version_lookup_[prepared_version] = &ms.version_runtime_data[prepared_version];
    ms.prepared_runtime_data.clear();

    return RuntimeError::Ok();
}

RuntimeError Vm::BuildVersionRuntimeData(const Chunk &chunk, VersionRuntimeData *out_data) {
    if (!out_data) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "output runtime data pointer is null");
    }

    VersionRuntimeData data;
    data.chunk = chunk;

    data.iconst_values.reserve(data.chunk.iconst.size());
    for (std::size_t i = 0; i < data.chunk.iconst.size(); ++i) {
        const std::int64_t v = data.chunk.iconst[i];
        if (v >= Value::kIntMinInline && v <= Value::kIntMaxInline) {
            data.iconst_values.push_back(Value::FromI64(v));
        } else {
            return RuntimeError::Error(RuntimeErrorCode::InvalidOperand,
                                       "hot reload currently requires inline-range iconst values");
        }
    }

    data.fconst_values.reserve(data.chunk.fconst.size());
    for (std::size_t i = 0; i < data.chunk.fconst.size(); ++i) {
        data.fconst_values.push_back(Value::FromF64(data.chunk.fconst[i]));
    }

    data.sconst_values.reserve(data.chunk.sconst.size());
    for (std::size_t i = 0; i < data.chunk.sconst.size(); ++i) {
        data.sconst_values.push_back(Value::FromObj((i + 1) << 1 | 1));
    }

    data.func_name_index.reserve(data.chunk.functions.size());
    for (std::size_t i = 0; i < data.chunk.functions.size(); ++i) {
        data.func_name_index[data.chunk.functions[i].name] = static_cast<std::uint16_t>(i);
    }

    data.global_name_index.reserve(data.chunk.global_names.size());
    for (std::size_t i = 0; i < data.chunk.global_names.size(); ++i) {
        data.global_name_index[data.chunk.global_names[i]] = static_cast<std::uint16_t>(i);
    }

    for (std::size_t ci = 0; ci < data.chunk.classes.size(); ++ci) {
        ClassInfo &cls = data.chunk.classes[ci];
        cls.field_name_index.reserve(cls.field_names.size());
        for (std::size_t fi = 0; fi < cls.field_names.size(); ++fi) {
            cls.field_name_index[cls.field_names[fi]] = static_cast<std::uint16_t>(fi);
        }
    }

    *out_data = std::move(data);
    return RuntimeError::Ok();
}

int Vm::FindFunctionInModule(const std::string &module_name, std::uint64_t version_id, const std::string &func_name) const {
    auto vit = version_lookup_.find(version_id);
    if (vit == version_lookup_.end()) return -1;
    return vit->second->FindFunction(func_name);
}

int Vm::FindGlobalInModule(const std::string &module_name, std::uint64_t version_id, const std::string &global_name) const {
    auto vit = version_lookup_.find(version_id);
    if (vit == version_lookup_.end()) return -1;
    return vit->second->FindGlobal(global_name);
}

RuntimeError Vm::BindExecutionVersion(const std::string &module_name, std::uint64_t module_version_id) {
    auto mit = module_states_.find(module_name);
    if (mit == module_states_.end()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "module state not found: " + module_name);
    }
    auto it = mit->second.version_runtime_data.find(module_version_id);
    if (it == mit->second.version_runtime_data.end()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "runtime data for target version not found");
    }

    bound_vrdata_ = &it->second;
    bound_module_version_id_ = module_version_id;
    bound_module_name_ = module_name;
    return RuntimeError::Ok();
}

RuntimeError Vm::BindExecutionVersion(std::uint64_t module_version_id) {
    return BindExecutionVersion(bound_module_name_, module_version_id);
}

Vm::ModuleRuntimeState *Vm::GetModuleState(const std::string &module_name) {
    auto it = module_states_.find(module_name);
    return it != module_states_.end() ? &it->second : nullptr;
}

const Vm::ModuleRuntimeState *Vm::GetModuleState(const std::string &module_name) const {
    auto it = module_states_.find(module_name);
    return it != module_states_.end() ? &it->second : nullptr;
}

RuntimeError Vm::EnsureModuleLoaded(const std::string &module_name,
                                    const std::string &relative_to_module,
                                    ModuleRuntimeState **out_state,
                                    std::string *out_resolved_name) {
    if (out_state) *out_state = nullptr;
    if (out_resolved_name) out_resolved_name->clear();
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "empty module name");
    }

    std::vector<std::string> candidates;
    candidates.push_back(module_name);

    std::size_t slash = relative_to_module.find('/');
    while (slash != std::string::npos) {
        std::string prefix = relative_to_module.substr(0, slash);
        if (!prefix.empty()) {
            std::string candidate = prefix + "/" + module_name;
            bool exists = false;
            for (const auto &it : candidates) {
                if (it == candidate) {
                    exists = true;
                    break;
                }
            }
            if (!exists) candidates.push_back(candidate);
        }
        slash = relative_to_module.find('/', slash + 1);
    }

    RuntimeError last_err = RuntimeError::Error(RuntimeErrorCode::NotFound, "module not found: " + module_name);
    for (const std::string &candidate : candidates) {
        ModuleRuntimeState *loaded = GetModuleState(candidate);
        if (loaded && loaded->active_version_id != 0) {
            if (out_state) *out_state = loaded;
            if (out_resolved_name) *out_resolved_name = candidate;
            return RuntimeError::Ok();
        }

        if (!module_loader_) {
            continue;
        }

        Chunk chunk;
        RuntimeError load_err = module_loader_(candidate, &chunk);
        if (!load_err.ok()) {
            last_err = load_err;
            continue;
        }

        RuntimeError module_err = LoadModule(candidate, chunk);
        if (!module_err.ok()) {
            last_err = module_err;
            continue;
        }
        loaded = GetModuleState(candidate);
        if (loaded && loaded->active_version_id != 0) {
            if (out_state) *out_state = loaded;
            if (out_resolved_name) *out_resolved_name = candidate;
            return RuntimeError::Ok();
        }
    }

    return last_err;
}

RuntimeError Vm::PrepareHotReload(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  std::uint64_t *out_candidate_version,
                                  HotReloadCompatReport *out_report,
                                  const MigrationDescriptor *migration) {
    std::uint64_t candidate_version = 0;
    RuntimeError e = hot_reload_manager_.PrepareHotReload(module_name, candidate, level, &candidate_version, out_report, migration);
    if (!e.ok()) {
        return e;
    }

    ModuleRuntimeState *ms = GetModuleState(module_name);
    if (!ms) {
        ms = &module_states_[module_name];
        ms->module_name = module_name;
    }

    VersionRuntimeData data;
    RuntimeError data_err = BuildVersionRuntimeData(candidate, &data);
    if (!data_err.ok()) {
        return data_err;
    }
    ms->prepared_runtime_data[candidate_version] = std::move(data);

    if (out_candidate_version) {
        *out_candidate_version = candidate_version;
    }
    return RuntimeError::Ok();
}

RuntimeError Vm::ActivateHotReload(const std::string &module_name,
                                   std::uint64_t candidate_version,
                                   std::uint64_t *out_previous_active) {
    ModuleRuntimeState *ms = GetModuleState(module_name);
    if (!ms) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "module state not found for activation");
    }

    auto pit = ms->prepared_runtime_data.find(candidate_version);
    if (pit == ms->prepared_runtime_data.end()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "prepared runtime data not found for candidate version");
    }

    RuntimeError e = hot_reload_manager_.ActivatePrepared(module_name, candidate_version, out_previous_active);
    if (!e.ok()) return e;

    ms->version_runtime_data[candidate_version] = std::move(pit->second);
    version_lookup_[candidate_version] = &ms->version_runtime_data[candidate_version];
    ms->prepared_runtime_data.erase(pit);
    ms->active_version_id = candidate_version;

    UpgradeClassInstancesForModule(module_name, candidate_version);

    if (module_name == current_module_name_) {
        current_module_version_id_ = candidate_version;
        if (frames_.empty()) {
            RuntimeError bind_err = BindExecutionVersion(module_name, candidate_version);
            if (!bind_err.ok()) return bind_err;
        }
    }
    return RuntimeError::Ok();
}

RuntimeError Vm::RollbackHotReload(const std::string &module_name, std::uint64_t version_id) {
    ModuleRuntimeState *ms = GetModuleState(module_name);
    if (!ms) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "module state not found for rollback");
    }

    RuntimeError e = hot_reload_manager_.RollbackHotReload(module_name, version_id);
    if (!e.ok()) return e;

    ms->prepared_runtime_data.clear();
    ms->active_version_id = version_id;

    if (module_name == current_module_name_) {
        current_module_version_id_ = version_id;
        if (frames_.empty()) {
            RuntimeError bind_err = BindExecutionVersion(module_name, version_id);
            if (!bind_err.ok()) return bind_err;
        }
    }
    return RuntimeError::Ok();
}

ModuleHotReloadStatus Vm::GetHotReloadStatus(const std::string &module_name) const {
    return hot_reload_manager_.GetHotReloadStatus(module_name);
}

RuntimeError Vm::RebindActiveChunkForCurrentModule() {
    std::uint64_t active_version = 0;
    const Chunk *active_chunk = hot_reload_manager_.GetActiveChunk(current_module_name_, &active_version);
    if (!active_chunk || active_version == 0) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "active chunk not available");
    }
    current_module_version_id_ = active_version;
    if (frames_.empty()) {
        RuntimeError bind_err = BindExecutionVersion(current_module_name_, active_version);
        if (!bind_err.ok()) return bind_err;
    }
    return RuntimeError::Ok();
}

const Chunk *Vm::GetChunkForVersion(std::uint64_t module_version_id) const {
    auto it = version_lookup_.find(module_version_id);
    if (it != version_lookup_.end()) return &it->second->chunk;
    return nullptr;
}

bool Vm::TryUpgradeObject(std::size_t obj_id) {
    if (obj_id == 0 || obj_id > objects_.size()) return false;
    LpcObject &obj = objects_[obj_id - 1];
    if (obj.destroyed || obj.module_name.empty()) return false;

    const ModuleRuntimeState *ms = GetModuleState(obj.module_name);
    if (!ms || ms->active_version_id == 0) return false;
    if (obj.module_version_id == ms->active_version_id) return false;

    auto new_it = ms->version_runtime_data.find(ms->active_version_id);
    if (new_it == ms->version_runtime_data.end()) return false;

    const Chunk &new_chunk = new_it->second.chunk;

    auto old_it = ms->version_runtime_data.find(obj.module_version_id);
    const std::vector<std::string> *old_global_names = nullptr;
    if (old_it != ms->version_runtime_data.end()) {
        old_global_names = &old_it->second.chunk.global_names;
    } else {
        old_global_names = &BoundChunk().global_names;
    }

    const MigrationDescriptor *migration = hot_reload_manager_.GetMigrationDescriptor(obj.module_name, ms->active_version_id);
    if (migration) {
        MigrateObjectGlobals(obj.globals, *old_global_names, new_chunk.global_names, *migration);
    } else {
        if (new_chunk.globals.size() > obj.globals.size()) {
            obj.globals.resize(new_chunk.globals.size(), Value::Nil());
        }
    }
    obj.module_version_id = ms->active_version_id;
    obj.blueprint = &new_chunk;
    return true;
}

void Vm::UpgradeClassInstancesForModule(const std::string &module_name, std::uint64_t new_version_id) {
    const ModuleRuntimeState *ms = GetModuleState(module_name);
    if (!ms) return;

    auto new_it = ms->version_runtime_data.find(new_version_id);
    if (new_it == ms->version_runtime_data.end()) return;
    const Chunk &new_chunk = new_it->second.chunk;

    const MigrationDescriptor *migration = hot_reload_manager_.GetMigrationDescriptor(module_name, new_version_id);

    std::unordered_map<std::string, const ClassMigration *> class_migration_map;
    if (migration) {
        for (const auto &cm : migration->classes) {
            class_migration_map[cm.class_name] = &cm;
        }
    }

    auto mci_it = module_class_instances_.find(module_name);
    if (mci_it == module_class_instances_.end()) return;

    for (std::size_t idx : mci_it->second) {
        if (idx >= class_fields_.size()) continue;
        if (class_module_version_ids_[idx] == new_version_id) continue;

        std::uint16_t tmpl_idx = class_template_ids_[idx];

        auto old_it = ms->version_runtime_data.find(class_module_version_ids_[idx]);
        const Chunk *old_chunk = nullptr;
        if (old_it != ms->version_runtime_data.end()) {
            old_chunk = &old_it->second.chunk;
        } else {
            old_chunk = &BoundChunk();
        }

        if (tmpl_idx >= old_chunk->classes.size() || tmpl_idx >= new_chunk.classes.size()) continue;

        const ClassInfo &old_ci = old_chunk->classes[tmpl_idx];
        const ClassInfo &new_ci = new_chunk.classes[tmpl_idx];

        if (old_ci.field_names == new_ci.field_names) {
            class_module_version_ids_[idx] = new_version_id;
            continue;
        }

        std::vector<Value> fields_vec;
        fields_vec.reserve(class_fields_[idx].Size());
        for (std::size_t fi = 0; fi < class_fields_[idx].Size(); ++fi) {
            fields_vec.push_back(class_fields_[idx].At(fi));
        }

        auto cm_it = class_migration_map.find(old_ci.name);
        if (cm_it != class_migration_map.end()) {
            MigrateClassFields(fields_vec, old_ci.field_names, new_ci.field_names, *cm_it->second);
        } else {
            const std::size_t old_size = fields_vec.size();
            fields_vec.resize(new_ci.nfields, MakeI64(0));
            for (std::size_t fi = old_size; fi < fields_vec.size(); ++fi) {
                fields_vec[fi] = DefaultForClassField(*this, new_ci, fi);
            }
        }

        class_fields_[idx] = LpcClass(new_ci.nfields, MakeI64(0));
        for (std::size_t fi = 0; fi < fields_vec.size() && fi < class_fields_[idx].Size(); ++fi) {
            class_fields_[idx].Set(fi, fields_vec[fi]);
        }

        class_module_version_ids_[idx] = new_version_id;
    }
}

RuntimeError Vm::RunInitCode() {
    if (BoundChunk().init_code.empty()) return {};

    std::vector<Value> globals;
    std::uint32_t ip = 0;
    while (ip < BoundChunk().init_code.size()) {
        std::uint8_t op = BoundChunk().init_code[ip++];
        if (op == static_cast<std::uint8_t>(Op::LoadIConst)) {
            if (ip + 2 > BoundChunk().init_code.size()) {
                RuntimeError e;
                e.message = "init LoadIConst truncated";
                return e;
            }
            std::uint16_t idx = static_cast<std::uint16_t>(BoundChunk().init_code[ip]) |
                static_cast<std::uint16_t>(BoundChunk().init_code[ip + 1] << 8);
            ip += 2;
            if (idx >= BoundChunk().iconst.size()) {
                RuntimeError e;
                e.message = "init iconst index out of range";
                return e;
            }
            value_stack_.push_back(BoundIConst()[idx]);
        } else if (op == static_cast<std::uint8_t>(Op::LoadFConst)) {
            if (ip + 2 > BoundChunk().init_code.size()) {
                RuntimeError e;
                e.message = "init LoadFConst truncated";
                return e;
            }
            std::uint16_t idx = static_cast<std::uint16_t>(BoundChunk().init_code[ip]) |
                static_cast<std::uint16_t>(BoundChunk().init_code[ip + 1] << 8);
            ip += 2;
            if (idx >= BoundFConst().size()) {
                RuntimeError e;
                e.message = "init fconst index out of range";
                return e;
            }
            value_stack_.push_back(BoundFConst()[idx]);
        } else if (op == static_cast<std::uint8_t>(Op::LoadSConst)) {
            if (ip + 2 > BoundChunk().init_code.size()) {
                RuntimeError e;
                e.message = "init LoadSConst truncated";
                return e;
            }
            std::uint16_t idx = static_cast<std::uint16_t>(BoundChunk().init_code[ip]) |
                static_cast<std::uint16_t>(BoundChunk().init_code[ip + 1] << 8);
            ip += 2;
            if (idx >= BoundSConst().size()) {
                RuntimeError e;
                e.message = "init sconst index out of range";
                return e;
            }
            value_stack_.push_back(BoundSConst()[idx]);
        } else if (op == static_cast<std::uint8_t>(Op::StoreGlobal)) {
            if (ip + 2 > BoundChunk().init_code.size()) {
                RuntimeError e;
                e.message = "init StoreGlobal truncated";
                return e;
            }
            std::uint16_t idx = static_cast<std::uint16_t>(BoundChunk().init_code[ip]) |
                static_cast<std::uint16_t>(BoundChunk().init_code[ip + 1] << 8);
            ip += 2;
            if (value_stack_.empty()) {
                RuntimeError e;
                e.message = "init StoreGlobal stack underflow";
                return e;
            }
            Value v = value_stack_.back();
            value_stack_.pop_back();
            if (idx >= globals.size()) {
                globals.resize(idx + 1, Value::Nil());
            }
            globals[idx] = v;
        } else if (op == static_cast<std::uint8_t>(Op::Return)) {
            break;
        } else {
            RuntimeError e;
            e.message = std::string("unsupported opcode in init_code: ") + std::to_string(static_cast<int>(op));
            return e;
        }
    }

    BoundChunk().globals = std::move(globals);
    value_stack_.clear();
    return {};
}
