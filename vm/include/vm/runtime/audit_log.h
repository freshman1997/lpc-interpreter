#ifndef LPC_VM_RUNTIME_AUDIT_LOG_H
#define LPC_VM_RUNTIME_AUDIT_LOG_H

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace lpc {
namespace vm {

enum class AuditAction {
    Prepare = 0,
    Activate = 1,
    Rollback = 2,
    Retire = 3,
};

struct AuditEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string module_name;
    AuditAction action = AuditAction::Prepare;
    std::uint64_t version_id = 0;
    std::uint64_t previous_active_version = 0;
    std::string chunk_hash;
    std::string level;
    std::string compat_result;
    std::string actor;
};

class AuditLog {
public:
    static constexpr std::size_t kDefaultMaxEntries = 1024;

    explicit AuditLog(std::size_t max_entries = kDefaultMaxEntries);

    void Record(AuditEntry entry);

    void SetFilePath(const std::string &path);
    const std::string &FilePath() const { return file_path_; }

    void Flush();

    std::size_t Size() const { return entries_.size(); }
    const AuditEntry &Entry(std::size_t index) const { return entries_[index]; }
    const std::vector<AuditEntry> &Entries() const { return entries_; }

    void Clear();

    static std::string FormatEntryJson(const AuditEntry &entry);
    static const char *ToString(AuditAction action);

private:
    void AppendToFile(const AuditEntry &entry);

    std::vector<AuditEntry> entries_;
    std::size_t max_entries_;
    std::string file_path_;
};

} // namespace vm
} // namespace lpc

#endif
