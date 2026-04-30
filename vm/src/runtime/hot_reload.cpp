#include "vm/runtime/hot_reload.h"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <unordered_set>

namespace lpc {
namespace vm {

namespace {

template <typename T>
void HashCombine(std::size_t *seed, const T &v) {
    if (!seed) return;
    const std::size_t h = std::hash<T>{}(v);
    *seed ^= h + 0x9e3779b97f4a7c15ull + (*seed << 6) + (*seed >> 2);
}

void AddIssue(HotReloadCompatReport *report, const std::string &field, const std::string &detail) {
    if (!report) return;
    report->issues.push_back({field, detail});
}

bool CompareClassLayout(const ClassInfo &lhs, const ClassInfo &rhs) {
    if (lhs.name != rhs.name) return false;
    if (lhs.nfields != rhs.nfields) return false;
    if (lhs.parent_class_idx != rhs.parent_class_idx) return false;
    if (lhs.field_names.size() != rhs.field_names.size()) return false;
    for (std::size_t i = 0; i < lhs.field_names.size(); ++i) {
        if (lhs.field_names[i] != rhs.field_names[i]) return false;
    }
    return true;
}

} // namespace

const char *ToString(HotReloadLevel level) {
    switch (level) {
    case HotReloadLevel::L0: return "L0";
    case HotReloadLevel::L1: return "L1";
    case HotReloadLevel::L2: return "L2";
    }
    return "Unknown";
}

const char *ToString(HotReloadCompatKind kind) {
    switch (kind) {
    case HotReloadCompatKind::Compatible: return "Compatible";
    case HotReloadCompatKind::CompatibleWithMigration: return "CompatibleWithMigration";
    case HotReloadCompatKind::Incompatible: return "Incompatible";
    }
    return "Unknown";
}

const char *ToString(ModuleVersionState state) {
    switch (state) {
    case ModuleVersionState::Loaded: return "Loaded";
    case ModuleVersionState::Active: return "Active";
    case ModuleVersionState::Deprecated: return "Deprecated";
    case ModuleVersionState::Retired: return "Retired";
    }
    return "Unknown";
}

HotReloadCompatReport HotReloadCompatChecker::Check(const Chunk &old_chunk, const Chunk &new_chunk, HotReloadLevel level) {
    switch (level) {
    case HotReloadLevel::L0:
        return CheckL0(old_chunk, new_chunk);
    case HotReloadLevel::L1:
        return CheckL1Impl(old_chunk, new_chunk);
    case HotReloadLevel::L2:
        HotReloadCompatReport report;
        report.kind = HotReloadCompatKind::CompatibleWithMigration;
        report.migration_required = true;
        return report;
    }
    HotReloadCompatReport report;
    report.kind = HotReloadCompatKind::Incompatible;
    AddIssue(&report, "allow_level", "unknown hot reload level");
    return report;
}

HotReloadCompatReport HotReloadCompatChecker::CheckL0(const Chunk &old_chunk, const Chunk &new_chunk) {
    HotReloadCompatReport report;
    report.kind = HotReloadCompatKind::Compatible;

    if (old_chunk.module_name != new_chunk.module_name) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "module_name", "module name mismatch");
    }

    if (old_chunk.functions.size() != new_chunk.functions.size()) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "functions", "function count mismatch");
    } else {
        for (std::size_t i = 0; i < old_chunk.functions.size(); ++i) {
            const FunctionProto &lhs = old_chunk.functions[i];
            const FunctionProto &rhs = new_chunk.functions[i];
            if (lhs.name != rhs.name) {
                report.kind = HotReloadCompatKind::Incompatible;
                AddIssue(&report, "functions", "function name changed at index " + std::to_string(i));
                continue;
            }
            if (lhs.arity != rhs.arity) {
                report.kind = HotReloadCompatKind::Incompatible;
                AddIssue(&report, "functions", "arity changed for function '" + lhs.name + "'");
            }
        }
    }

    if (old_chunk.classes.size() != new_chunk.classes.size()) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "classes", "class count mismatch");
    } else {
        for (std::size_t i = 0; i < old_chunk.classes.size(); ++i) {
            if (!CompareClassLayout(old_chunk.classes[i], new_chunk.classes[i])) {
                report.kind = HotReloadCompatKind::Incompatible;
                AddIssue(&report, "classes", "class layout mismatch at index " + std::to_string(i));
            }
        }
    }

    if (old_chunk.global_names.size() != new_chunk.global_names.size()) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "globals", "global count mismatch");
    } else {
        for (std::size_t i = 0; i < old_chunk.global_names.size(); ++i) {
            if (old_chunk.global_names[i] != new_chunk.global_names[i]) {
                report.kind = HotReloadCompatKind::Incompatible;
                AddIssue(&report, "globals", "global order/name changed at index " + std::to_string(i));
            }
        }
    }

    return report;
}

HotReloadCompatReport HotReloadCompatChecker::CheckL1Impl(const Chunk &old_chunk, const Chunk &new_chunk) {
    HotReloadCompatReport report;
    report.kind = HotReloadCompatKind::Compatible;

    if (old_chunk.module_name != new_chunk.module_name) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "module_name", "module name mismatch");
    }

    if (new_chunk.functions.size() < old_chunk.functions.size()) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "functions", "function count decreased (L1 only allows additions)");
    }
    for (std::size_t i = 0; i < old_chunk.functions.size() && i < new_chunk.functions.size(); ++i) {
        const FunctionProto &lhs = old_chunk.functions[i];
        const FunctionProto &rhs = new_chunk.functions[i];
        if (lhs.name != rhs.name) {
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "functions", "existing function name changed at index " + std::to_string(i));
            continue;
        }
        if (lhs.arity != rhs.arity) {
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "functions", "arity changed for existing function '" + lhs.name + "'");
        }
    }
    if (new_chunk.functions.size() > old_chunk.functions.size()) {
        report.added_functions = new_chunk.functions.size() - old_chunk.functions.size();
        for (std::size_t i = old_chunk.functions.size(); i < new_chunk.functions.size(); ++i) {
            report.added_function_names.push_back(new_chunk.functions[i].name);
        }
    }

    if (new_chunk.classes.size() < old_chunk.classes.size()) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "classes", "class count decreased (L1 only allows additions)");
    }
    for (std::size_t i = 0; i < old_chunk.classes.size() && i < new_chunk.classes.size(); ++i) {
        if (!CompareClassLayout(old_chunk.classes[i], new_chunk.classes[i])) {
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "classes", "existing class layout mismatch at index " + std::to_string(i));
        }
    }
    if (new_chunk.classes.size() > old_chunk.classes.size()) {
        report.added_classes = new_chunk.classes.size() - old_chunk.classes.size();
        for (std::size_t i = old_chunk.classes.size(); i < new_chunk.classes.size(); ++i) {
            report.added_class_names.push_back(new_chunk.classes[i].name);
        }
    }

    if (new_chunk.global_names.size() < old_chunk.global_names.size()) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "globals", "global count decreased (L1 only allows additions)");
    }
    for (std::size_t i = 0; i < old_chunk.global_names.size() && i < new_chunk.global_names.size(); ++i) {
        if (old_chunk.global_names[i] != new_chunk.global_names[i]) {
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "globals", "existing global order/name changed at index " + std::to_string(i));
        }
    }
    if (new_chunk.global_names.size() > old_chunk.global_names.size()) {
        report.added_globals = new_chunk.global_names.size() - old_chunk.global_names.size();
        for (std::size_t i = old_chunk.global_names.size(); i < new_chunk.global_names.size(); ++i) {
            report.added_global_names.push_back(new_chunk.global_names[i]);
        }
    }

    return report;
}

RuntimeError ModuleRegistry::InstallCandidate(const std::string &module_name,
                                              std::shared_ptr<const Chunk> chunk,
                                              std::uint64_t *out_version_id) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "empty module name for candidate install");
    }
    if (!chunk) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "candidate chunk is null");
    }
    if (!chunk->module_name.empty() && chunk->module_name != module_name) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "candidate module name mismatch");
    }

    ModuleChain &chain = modules_[module_name];
    const std::uint64_t version_id = next_global_version_id_++;

    ModuleVersionEntry entry;
    entry.version_id = version_id;
    entry.chunk = std::move(chunk);
    entry.state = ModuleVersionState::Loaded;
    entry.ref_count = 0;
    entry.chunk_hash = BuildChunkHash(*entry.chunk);

    chain.versions[version_id] = std::move(entry);
    if (out_version_id) {
        *out_version_id = version_id;
    }
    return RuntimeError::Ok();
}

RuntimeError ModuleRegistry::ActivateVersion(const std::string &module_name,
                                             std::uint64_t version_id,
                                             std::uint64_t *out_previous_active) {
    ModuleVersionEntry *next = nullptr;
    RuntimeError e = GetVersionEntry(module_name, version_id, &next);
    if (!e.ok()) return e;

    ModuleChain &chain = modules_[module_name];
    const std::uint64_t prev_active = chain.active_version;
    if (out_previous_active) {
        *out_previous_active = prev_active;
    }

    if (prev_active != 0 && prev_active != version_id) {
        auto it_prev = chain.versions.find(prev_active);
        if (it_prev != chain.versions.end() && it_prev->second.state != ModuleVersionState::Retired) {
            it_prev->second.state = ModuleVersionState::Deprecated;
        }
    }

    next->state = ModuleVersionState::Active;
    chain.active_version = version_id;
    RetireDeprecatedVersions(&chain, 2);
    return RuntimeError::Ok();
}

RuntimeError ModuleRegistry::PinVersion(const std::string &module_name, std::uint64_t version_id) {
    ModuleVersionEntry *entry = nullptr;
    RuntimeError e = GetVersionEntry(module_name, version_id, &entry);
    if (!e.ok()) return e;
    ++entry->ref_count;
    return RuntimeError::Ok();
}

RuntimeError ModuleRegistry::UnpinVersion(const std::string &module_name, std::uint64_t version_id) {
    ModuleVersionEntry *entry = nullptr;
    RuntimeError e = GetVersionEntry(module_name, version_id, &entry);
    if (!e.ok()) return e;
    if (entry->ref_count == 0) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "version refcount underflow");
    }
    --entry->ref_count;
    if (entry->ref_count == 0) {
        auto it = modules_.find(module_name);
        if (it != modules_.end()) {
            RetireDeprecatedVersions(&it->second, 2);
        }
    }
    return RuntimeError::Ok();
}

RuntimeError ModuleRegistry::RollbackTo(const std::string &module_name, std::uint64_t version_id) {
    return ActivateVersion(module_name, version_id, nullptr);
}

bool ModuleRegistry::HasActiveVersion(const std::string &module_name) const {
    auto it = modules_.find(module_name);
    if (it == modules_.end()) return false;
    if (it->second.active_version == 0) return false;
    return it->second.versions.find(it->second.active_version) != it->second.versions.end();
}

const Chunk *ModuleRegistry::GetActiveChunk(const std::string &module_name, std::uint64_t *out_version_id) const {
    auto it = modules_.find(module_name);
    if (it == modules_.end()) return nullptr;
    const ModuleChain &chain = it->second;
    auto vit = chain.versions.find(chain.active_version);
    if (vit == chain.versions.end() || !vit->second.chunk) return nullptr;
    if (out_version_id) {
        *out_version_id = vit->second.version_id;
    }
    return vit->second.chunk.get();
}

ModuleHotReloadStatus ModuleRegistry::GetStatus(const std::string &module_name) const {
    ModuleHotReloadStatus out;
    out.module_name = module_name;

    auto it = modules_.find(module_name);
    if (it == modules_.end()) {
        return out;
    }

    const ModuleChain &chain = it->second;
    out.active_version = chain.active_version;
    for (const auto &kv : chain.versions) {
        ModuleVersionSnapshot snap;
        snap.version_id = kv.second.version_id;
        snap.state = kv.second.state;
        snap.ref_count = kv.second.ref_count;
        snap.chunk_hash = kv.second.chunk_hash;
        out.versions.push_back(std::move(snap));
    }
    std::sort(out.versions.begin(), out.versions.end(),
              [](const ModuleVersionSnapshot &a, const ModuleVersionSnapshot &b) {
                  return a.version_id > b.version_id;
              });
    return out;
}

std::string ModuleRegistry::BuildChunkHash(const Chunk &chunk) {
    std::size_t seed = 0;
    HashCombine(&seed, chunk.module_name);
    HashCombine(&seed, chunk.format_version);
    HashCombine(&seed, chunk.flags);
    HashCombine(&seed, chunk.code.size());
    HashCombine(&seed, chunk.iconst.size());
    HashCombine(&seed, chunk.fconst.size());
    HashCombine(&seed, chunk.sconst.size());
    HashCombine(&seed, chunk.functions.size());
    HashCombine(&seed, chunk.classes.size());
    for (const auto &f : chunk.functions) {
        HashCombine(&seed, f.name);
        HashCombine(&seed, f.arity);
        HashCombine(&seed, f.nlocals);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zx", seed);
    return std::string(buf);
}

void ModuleRegistry::RetireDeprecatedVersions(ModuleChain *chain, std::size_t keep_deprecated_limit) {
    if (!chain) return;

    std::vector<std::uint64_t> deprecated;
    deprecated.reserve(std::min<std::size_t>(chain->versions.size(), 8));
    for (const auto &kv : chain->versions) {
        if (kv.second.state == ModuleVersionState::Deprecated) {
            deprecated.push_back(kv.first);
        }
    }

    if (deprecated.size() <= keep_deprecated_limit) return;

    std::sort(deprecated.begin(), deprecated.end(), std::greater<std::uint64_t>());
    for (std::size_t i = keep_deprecated_limit; i < deprecated.size(); ++i) {
        auto it = chain->versions.find(deprecated[i]);
        if (it == chain->versions.end()) continue;
        if (it->second.ref_count != 0) continue;
        it->second.state = ModuleVersionState::Retired;
        chain->versions.erase(it);
    }
}

RuntimeError ModuleRegistry::GetVersionEntry(const std::string &module_name,
                                             std::uint64_t version_id,
                                             ModuleVersionEntry **out_entry) {
    if (!out_entry) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "output entry pointer is null");
    }
    auto it = modules_.find(module_name);
    if (it == modules_.end()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "module not found in registry");
    }
    auto vit = it->second.versions.find(version_id);
    if (vit == it->second.versions.end()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "module version not found");
    }
    *out_entry = &vit->second;
    return RuntimeError::Ok();
}

RuntimeError HotReloadManager::PrepareHotReload(const std::string &module_name,
                                                const Chunk &candidate,
                                                HotReloadLevel level,
                                                std::uint64_t *out_candidate_version,
                                                HotReloadCompatReport *out_report,
                                                const MigrationDescriptor *migration) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "empty module name for hot reload");
    }
    if (!candidate.module_name.empty() && candidate.module_name != module_name) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot reload candidate module mismatch");
    }

    HotReloadCompatReport report;
    if (registry_.HasActiveVersion(module_name)) {
        const Chunk *old_chunk = registry_.GetActiveChunk(module_name, nullptr);
        if (!old_chunk) {
            return RuntimeError::Error(RuntimeErrorCode::InternalError, "active module missing chunk data");
        }
        if (level == HotReloadLevel::L2 && migration) {
            report = HotReloadCompatChecker::CheckL2(*old_chunk, candidate, *migration);
        } else {
            report = HotReloadCompatChecker::Check(*old_chunk, candidate, level);
        }
        if (!report.ok()) {
            if (out_report) {
                *out_report = report;
            }
            return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot reload compatibility check failed");
        }
    } else {
        report.kind = HotReloadCompatKind::Compatible;
    }

    std::uint64_t version_id = 0;
    RuntimeError e = registry_.InstallCandidate(module_name,
                                                std::make_shared<Chunk>(candidate),
                                                &version_id);
    if (!e.ok()) {
        return e;
    }

    if (level == HotReloadLevel::L2 && migration) {
        migration_descriptors_[version_id] = *migration;
    }

    prepared_versions_[module_name] = version_id;
    if (out_candidate_version) {
        *out_candidate_version = version_id;
    }
    if (out_report) {
        *out_report = report;
    }

    {
        AuditEntry ae;
        ae.module_name = module_name;
        ae.action = AuditAction::Prepare;
        ae.version_id = version_id;
        ae.level = ToString(level);
        ae.compat_result = ToString(report.kind);
        ae.chunk_hash = registry_.GetStatus(module_name).versions.empty()
            ? "" : [&] {
                for (const auto &v : registry_.GetStatus(module_name).versions) {
                    if (v.version_id == version_id) return v.chunk_hash;
                }
                return std::string{};
            }();
        audit_log_.Record(std::move(ae));
    }

    return RuntimeError::Ok();
}

RuntimeError HotReloadManager::ActivatePrepared(const std::string &module_name,
                                                std::uint64_t prepared_version,
                                                std::uint64_t *out_previous_active) {
    auto it = prepared_versions_.find(module_name);
    if (it == prepared_versions_.end()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "no prepared hot reload candidate");
    }
    if (it->second != prepared_version) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "prepared version mismatch");
    }
    std::uint64_t prev_active = 0;
    RuntimeError e = registry_.ActivateVersion(module_name, prepared_version, &prev_active);
    if (!e.ok()) {
        return e;
    }
    if (out_previous_active) {
        *out_previous_active = prev_active;
    }
    prepared_versions_.erase(it);

    {
        AuditEntry ae;
        ae.module_name = module_name;
        ae.action = AuditAction::Activate;
        ae.version_id = prepared_version;
        ae.previous_active_version = prev_active;
        audit_log_.Record(std::move(ae));
    }

    return RuntimeError::Ok();
}

RuntimeError HotReloadManager::RollbackHotReload(const std::string &module_name, std::uint64_t version_id) {
    RuntimeError e = registry_.RollbackTo(module_name, version_id);
    if (!e.ok()) return e;

    AuditEntry ae;
    ae.module_name = module_name;
    ae.action = AuditAction::Rollback;
    ae.version_id = version_id;
    audit_log_.Record(std::move(ae));

    return RuntimeError::Ok();
}

RuntimeError HotReloadManager::PinVersion(const std::string &module_name, std::uint64_t version_id) {
    return registry_.PinVersion(module_name, version_id);
}

RuntimeError HotReloadManager::UnpinVersion(const std::string &module_name, std::uint64_t version_id) {
    return registry_.UnpinVersion(module_name, version_id);
}

bool HotReloadManager::HasActiveModule(const std::string &module_name) const {
    return registry_.HasActiveVersion(module_name);
}

const Chunk *HotReloadManager::GetActiveChunk(const std::string &module_name, std::uint64_t *out_version_id) const {
    return registry_.GetActiveChunk(module_name, out_version_id);
}

ModuleHotReloadStatus HotReloadManager::GetHotReloadStatus(const std::string &module_name) const {
    ModuleHotReloadStatus status = registry_.GetStatus(module_name);
    auto it = prepared_versions_.find(module_name);
    if (it != prepared_versions_.end()) {
        status.prepared_version = it->second;
    }
    return status;
}

const char *ToString(FieldMigrationKind kind) {
    switch (kind) {
    case FieldMigrationKind::Keep: return "Keep";
    case FieldMigrationKind::Rename: return "Rename";
    case FieldMigrationKind::Drop: return "Drop";
    case FieldMigrationKind::AddWithDefault: return "AddWithDefault";
    }
    return "Unknown";
}

HotReloadCompatReport HotReloadCompatChecker::CheckL2(const Chunk &old_chunk,
                                                       const Chunk &new_chunk,
                                                       const MigrationDescriptor &migration) {
    HotReloadCompatReport report;
    report.kind = HotReloadCompatKind::CompatibleWithMigration;
    report.migration_required = true;

    if (old_chunk.module_name != new_chunk.module_name) {
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "module_name", "module name mismatch");
        return report;
    }

    std::unordered_map<std::string, FieldMigration> global_map;
    for (const auto &fm : migration.globals.entries) {
        if (fm.kind == FieldMigrationKind::Rename) {
            global_map[fm.old_name] = fm;
        } else if (fm.kind == FieldMigrationKind::Drop) {
            global_map[fm.old_name] = fm;
        }
    }

    std::unordered_map<std::string, std::string> rename_map;
    for (const auto &fm : migration.globals.entries) {
        if (fm.kind == FieldMigrationKind::Rename) {
            rename_map[fm.old_name] = fm.new_name;
        }
    }

    std::unordered_set<std::string> new_global_set(new_chunk.global_names.begin(), new_chunk.global_names.end());

    for (const auto &old_name : old_chunk.global_names) {
        if (new_global_set.count(old_name)) continue;
        bool covered = false;
        auto it = rename_map.find(old_name);
        if (it != rename_map.end() && new_global_set.count(it->second)) {
            covered = true;
        }
        auto drop_it = global_map.find(old_name);
        if (drop_it != global_map.end() && drop_it->second.kind == FieldMigrationKind::Drop) {
            covered = true;
        }
        if (!covered) {
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "globals", "old global '" + old_name + "' removed without migration");
        }
    }

    std::unordered_set<std::string> old_global_set(old_chunk.global_names.begin(), old_chunk.global_names.end());
    std::unordered_set<std::string> rename_targets;
    for (const auto &kv : rename_map) {
        rename_targets.insert(kv.second);
    }
    std::unordered_set<std::string> add_with_default_names;
    for (const auto &fm : migration.globals.entries) {
        if (fm.kind == FieldMigrationKind::AddWithDefault) {
            add_with_default_names.insert(fm.new_name);
        }
    }

    for (const auto &new_name : new_chunk.global_names) {
        if (old_global_set.count(new_name)) continue;
        if (rename_targets.count(new_name)) continue;
        if (add_with_default_names.count(new_name)) continue;
        report.kind = HotReloadCompatKind::Incompatible;
        AddIssue(&report, "globals", "new global '" + new_name + "' added without default value");
    }

    std::unordered_set<std::string> old_class_names;
    for (const auto &ci : old_chunk.classes) old_class_names.insert(ci.name);

    for (const auto &cm : migration.classes) {
        if (!old_class_names.count(cm.class_name)) {
            AddIssue(&report, "classes", "migration references non-existent old class '" + cm.class_name + "'");
        }
    }

    std::unordered_map<std::string, const ClassMigration *> class_mig_map;
    for (const auto &cm : migration.classes) {
        class_mig_map[cm.class_name] = &cm;
    }

    std::unordered_map<std::string, const ClassInfo *> new_class_map;
    for (const auto &ci : new_chunk.classes) new_class_map[ci.name] = &ci;

    for (const auto &old_ci : old_chunk.classes) {
        auto nci_it = new_class_map.find(old_ci.name);
        if (nci_it == new_class_map.end()) continue;
        const ClassInfo *new_ci = nci_it->second;
        if (old_ci.field_names == new_ci->field_names) continue;

        auto cm_it = class_mig_map.find(old_ci.name);
        if (cm_it == class_mig_map.end()) {
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "classes", "class '" + old_ci.name + "' field layout changed without ClassMigration");
            continue;
        }

        const ClassMigration &cm = *cm_it->second;
        std::unordered_map<std::string, std::string> cm_rename_map;
        std::unordered_set<std::string> cm_dropped;
        for (const auto &fm : cm.fields) {
            if (fm.kind == FieldMigrationKind::Rename) {
                cm_rename_map[fm.old_name] = fm.new_name;
            } else if (fm.kind == FieldMigrationKind::Drop) {
                cm_dropped.insert(fm.old_name);
            }
        }

        std::unordered_set<std::string> new_field_set(new_ci->field_names.begin(), new_ci->field_names.end());
        std::unordered_set<std::string> old_field_set(old_ci.field_names.begin(), old_ci.field_names.end());

        for (const auto &old_fn : old_ci.field_names) {
            if (new_field_set.count(old_fn)) continue;
            bool covered = false;
            auto rn_it = cm_rename_map.find(old_fn);
            if (rn_it != cm_rename_map.end() && new_field_set.count(rn_it->second)) {
                covered = true;
            }
            if (cm_dropped.count(old_fn)) covered = true;
            if (!covered) {
                report.kind = HotReloadCompatKind::Incompatible;
                AddIssue(&report, "classes", "class '" + old_ci.name + "' old field '" + old_fn + "' removed without migration");
            }
        }

        std::unordered_set<std::string> cm_rename_targets;
        for (const auto &kv : cm_rename_map) {
            cm_rename_targets.insert(kv.second);
        }
        std::unordered_set<std::string> cm_add_defaults;
        for (const auto &fm : cm.fields) {
            if (fm.kind == FieldMigrationKind::AddWithDefault) {
                cm_add_defaults.insert(fm.new_name);
            }
        }

        for (const auto &new_fn : new_ci->field_names) {
            if (old_field_set.count(new_fn)) continue;
            if (cm_rename_targets.count(new_fn)) continue;
            if (cm_add_defaults.count(new_fn)) continue;
            report.kind = HotReloadCompatKind::Incompatible;
            AddIssue(&report, "classes", "class '" + old_ci.name + "' new field '" + new_fn + "' added without default value");
        }
    }

    return report;
}

bool MigrateObjectGlobals(std::vector<Value> &globals,
                           const std::vector<std::string> &old_names,
                           const std::vector<std::string> &new_names,
                           const MigrationDescriptor &migration) {
    std::unordered_map<std::string, std::size_t> old_idx;
    for (std::size_t i = 0; i < old_names.size(); ++i) {
        old_idx[old_names[i]] = i;
    }

    std::unordered_map<std::string, FieldMigration> rename_map;
    std::unordered_set<std::string> dropped;
    for (const auto &fm : migration.globals.entries) {
        if (fm.kind == FieldMigrationKind::Rename) {
            rename_map[fm.old_name] = fm;
        } else if (fm.kind == FieldMigrationKind::Drop) {
            dropped.insert(fm.old_name);
        }
    }

    std::vector<Value> new_globals(new_names.size(), Value::Nil());

    for (std::size_t ni = 0; ni < new_names.size(); ++ni) {
        const std::string &new_name = new_names[ni];

        auto old_it = old_idx.find(new_name);
        if (old_it != old_idx.end()) {
            if (old_it->second < globals.size()) {
                new_globals[ni] = globals[old_it->second];
            }
            continue;
        }

        bool is_rename_target = false;
        for (const auto &kv : rename_map) {
            if (kv.second.new_name == new_name) {
                auto src_it = old_idx.find(kv.second.old_name);
                if (src_it != old_idx.end() && src_it->second < globals.size()) {
                    new_globals[ni] = globals[src_it->second];
                }
                is_rename_target = true;
                break;
            }
        }
        if (is_rename_target) continue;

        for (const auto &fm : migration.globals.entries) {
            if (fm.kind == FieldMigrationKind::AddWithDefault && fm.new_name == new_name) {
                new_globals[ni] = fm.default_value;
                break;
            }
        }
    }

    globals = std::move(new_globals);

    if (migration.custom_transform) {
        migration.custom_transform(globals);
    }

    return true;
}

bool MigrateClassFields(std::vector<Value> &fields,
                         const std::vector<std::string> &old_field_names,
                         const std::vector<std::string> &new_field_names,
                         const ClassMigration &class_migration) {
    std::unordered_map<std::string, std::size_t> old_idx;
    for (std::size_t i = 0; i < old_field_names.size(); ++i) {
        old_idx[old_field_names[i]] = i;
    }

    std::unordered_map<std::string, FieldMigration> rename_map;
    std::unordered_set<std::string> dropped;
    for (const auto &fm : class_migration.fields) {
        if (fm.kind == FieldMigrationKind::Rename) {
            rename_map[fm.old_name] = fm;
        } else if (fm.kind == FieldMigrationKind::Drop) {
            dropped.insert(fm.old_name);
        }
    }

    std::vector<Value> new_fields(new_field_names.size(), Value::Nil());

    for (std::size_t ni = 0; ni < new_field_names.size(); ++ni) {
        const std::string &new_name = new_field_names[ni];

        auto old_it = old_idx.find(new_name);
        if (old_it != old_idx.end()) {
            if (old_it->second < fields.size()) {
                new_fields[ni] = fields[old_it->second];
            }
            continue;
        }

        bool is_rename_target = false;
        for (const auto &kv : rename_map) {
            if (kv.second.new_name == new_name) {
                auto src_it = old_idx.find(kv.second.old_name);
                if (src_it != old_idx.end() && src_it->second < fields.size()) {
                    new_fields[ni] = fields[src_it->second];
                }
                is_rename_target = true;
                break;
            }
        }
        if (is_rename_target) continue;

        for (const auto &fm : class_migration.fields) {
            if (fm.kind == FieldMigrationKind::AddWithDefault && fm.new_name == new_name) {
                new_fields[ni] = fm.default_value;
                break;
            }
        }
    }

    fields = std::move(new_fields);
    return true;
}

const MigrationDescriptor *HotReloadManager::GetMigrationDescriptor(const std::string &module_name,
                                                                     std::uint64_t version_id) const {
    auto it = migration_descriptors_.find(version_id);
    if (it == migration_descriptors_.end()) return nullptr;
    return &it->second;
}

} // namespace vm
} // namespace lpc
