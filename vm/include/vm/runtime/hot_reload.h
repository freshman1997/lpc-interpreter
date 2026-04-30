#ifndef LPC_VM_RUNTIME_HOT_RELOAD_H
#define LPC_VM_RUNTIME_HOT_RELOAD_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vm/bytecode/chunk.h"
#include "vm/runtime/error.h"
#include "vm/runtime/audit_log.h"
#include "vm/value/value.h"

namespace lpc {
namespace vm {

enum class HotReloadLevel {
    L0 = 0,
    L1 = 1,
    L2 = 2,
};

enum class HotReloadCompatKind {
    Compatible = 0,
    CompatibleWithMigration = 1,
    Incompatible = 2,
};

struct HotReloadCompatIssue {
    std::string field;
    std::string detail;
};

struct HotReloadCompatReport {
    HotReloadCompatKind kind = HotReloadCompatKind::Incompatible;
    bool migration_required = false;
    std::vector<HotReloadCompatIssue> issues;

    std::size_t added_functions = 0;
    std::size_t added_classes = 0;
    std::size_t added_globals = 0;
    std::vector<std::string> added_function_names;
    std::vector<std::string> added_class_names;
    std::vector<std::string> added_global_names;

    bool ok() const {
        return kind == HotReloadCompatKind::Compatible ||
            kind == HotReloadCompatKind::CompatibleWithMigration;
    }
};

enum class ModuleVersionState {
    Loaded = 0,
    Active = 1,
    Deprecated = 2,
    Retired = 3,
};

enum class FieldMigrationKind {
    Keep = 0,
    Rename = 1,
    Drop = 2,
    AddWithDefault = 3,
};

struct FieldMigration {
    std::string old_name;
    std::string new_name;
    FieldMigrationKind kind = FieldMigrationKind::Keep;
    Value default_value = Value::Nil();
};

struct ClassMigration {
    std::string class_name;
    std::vector<FieldMigration> fields;
};

struct GlobalMigration {
    std::vector<FieldMigration> entries;
};

using MigrationFn = std::function<void(std::vector<Value>&)>;

struct MigrationDescriptor {
    GlobalMigration globals;
    std::vector<ClassMigration> classes;
    MigrationFn custom_transform;
};

struct ModuleVersionSnapshot {
    std::uint64_t version_id = 0;
    ModuleVersionState state = ModuleVersionState::Loaded;
    std::size_t ref_count = 0;
    std::string chunk_hash;
};

struct ModuleHotReloadStatus {
    std::string module_name;
    std::uint64_t active_version = 0;
    std::uint64_t prepared_version = 0;
    std::vector<ModuleVersionSnapshot> versions;
};

class HotReloadCompatChecker {
public:
    static HotReloadCompatReport Check(const Chunk &old_chunk, const Chunk &new_chunk, HotReloadLevel level);
    static HotReloadCompatReport CheckL2(const Chunk &old_chunk, const Chunk &new_chunk,
                                          const MigrationDescriptor &migration);

private:
    static HotReloadCompatReport CheckL0(const Chunk &old_chunk, const Chunk &new_chunk);
    static HotReloadCompatReport CheckL1Impl(const Chunk &old_chunk, const Chunk &new_chunk);
};

class ModuleRegistry {
public:
    RuntimeError InstallCandidate(const std::string &module_name,
                                  std::shared_ptr<const Chunk> chunk,
                                  std::uint64_t *out_version_id);
    RuntimeError ActivateVersion(const std::string &module_name,
                                 std::uint64_t version_id,
                                 std::uint64_t *out_previous_active);
    RuntimeError PinVersion(const std::string &module_name, std::uint64_t version_id);
    RuntimeError UnpinVersion(const std::string &module_name, std::uint64_t version_id);
    RuntimeError RollbackTo(const std::string &module_name, std::uint64_t version_id);

    bool HasActiveVersion(const std::string &module_name) const;
    const Chunk *GetActiveChunk(const std::string &module_name, std::uint64_t *out_version_id = nullptr) const;
    ModuleHotReloadStatus GetStatus(const std::string &module_name) const;

private:
    struct ModuleVersionEntry {
        std::uint64_t version_id = 0;
        std::shared_ptr<const Chunk> chunk;
        ModuleVersionState state = ModuleVersionState::Loaded;
        std::size_t ref_count = 0;
        std::string chunk_hash;
    };

    struct ModuleChain {
        std::uint64_t next_version_id = 1;
        std::uint64_t active_version = 0;
        std::unordered_map<std::uint64_t, ModuleVersionEntry> versions;
    };

    static std::string BuildChunkHash(const Chunk &chunk);
    static void RetireDeprecatedVersions(ModuleChain *chain, std::size_t keep_deprecated_limit);
    RuntimeError GetVersionEntry(const std::string &module_name,
                                 std::uint64_t version_id,
                                 ModuleVersionEntry **out_entry);

    std::unordered_map<std::string, ModuleChain> modules_;
    std::uint64_t next_global_version_id_ = 1;
};

class HotReloadManager {
public:
    RuntimeError PrepareHotReload(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  std::uint64_t *out_candidate_version,
                                  HotReloadCompatReport *out_report,
                                  const MigrationDescriptor *migration = nullptr);
    RuntimeError ActivatePrepared(const std::string &module_name,
                                  std::uint64_t prepared_version,
                                  std::uint64_t *out_previous_active = nullptr);
    RuntimeError RollbackHotReload(const std::string &module_name, std::uint64_t version_id);
    RuntimeError PinVersion(const std::string &module_name, std::uint64_t version_id);
    RuntimeError UnpinVersion(const std::string &module_name, std::uint64_t version_id);

    bool HasActiveModule(const std::string &module_name) const;
    const Chunk *GetActiveChunk(const std::string &module_name, std::uint64_t *out_version_id = nullptr) const;
    ModuleHotReloadStatus GetHotReloadStatus(const std::string &module_name) const;
    const MigrationDescriptor *GetMigrationDescriptor(const std::string &module_name, std::uint64_t version_id) const;

    AuditLog &audit_log() { return audit_log_; }
    const AuditLog &audit_log() const { return audit_log_; }

private:
    ModuleRegistry registry_;
    std::unordered_map<std::string, std::uint64_t> prepared_versions_;
    std::unordered_map<std::uint64_t, MigrationDescriptor> migration_descriptors_;
    AuditLog audit_log_;
};

const char *ToString(HotReloadLevel level);
const char *ToString(HotReloadCompatKind kind);
const char *ToString(ModuleVersionState state);
const char *ToString(FieldMigrationKind kind);

bool MigrateObjectGlobals(std::vector<Value> &globals,
                           const std::vector<std::string> &old_names,
                           const std::vector<std::string> &new_names,
                           const MigrationDescriptor &migration);

bool MigrateClassFields(std::vector<Value> &fields,
                         const std::vector<std::string> &old_field_names,
                         const std::vector<std::string> &new_field_names,
                         const ClassMigration &class_migration);

} // namespace vm
} // namespace lpc

#endif
