#include "vm/runtime/audit_log.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace lpc {
namespace vm {

AuditLog::AuditLog(std::size_t max_entries)
    : max_entries_(max_entries) {}

void AuditLog::Record(AuditEntry entry) {
    if (entry.timestamp == std::chrono::system_clock::time_point{}) {
        entry.timestamp = std::chrono::system_clock::now();
    }

    if (entries_.size() >= max_entries_) {
        std::size_t trim = max_entries_ / 4;
        if (trim == 0) trim = 1;
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(trim));
    }
    entries_.push_back(std::move(entry));

    if (!file_path_.empty()) {
        AppendToFile(entries_.back());
    }
}

void AuditLog::SetFilePath(const std::string &path) {
    file_path_ = path;
}

void AuditLog::Flush() {
    if (file_path_.empty()) return;

    std::ofstream out(file_path_, std::ios::app | std::ios::binary);
    if (!out.is_open()) return;

    for (const auto &e : entries_) {
        out << FormatEntryJson(e) << "\n";
    }
    out.flush();
}

void AuditLog::Clear() {
    entries_.clear();
}

void AuditLog::AppendToFile(const AuditEntry &entry) {
    std::ofstream out(file_path_, std::ios::app | std::ios::binary);
    if (!out.is_open()) return;
    out << FormatEntryJson(entry) << "\n";
    out.flush();
}

const char *AuditLog::ToString(AuditAction action) {
    switch (action) {
    case AuditAction::Prepare:  return "prepare";
    case AuditAction::Activate: return "activate";
    case AuditAction::Rollback: return "rollback";
    case AuditAction::Retire:   return "retire";
    }
    return "unknown";
}

static std::string FormatTimestamp(const std::chrono::system_clock::time_point &tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

static std::string JsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    return out;
}

std::string AuditLog::FormatEntryJson(const AuditEntry &entry) {
    std::ostringstream oss;
    oss << "{\"ts\":\"" << FormatTimestamp(entry.timestamp) << "\""
        << ",\"module\":\"" << JsonEscape(entry.module_name) << "\""
        << ",\"action\":\"" << ToString(entry.action) << "\""
        << ",\"version\":" << entry.version_id
        << ",\"prev_active\":" << entry.previous_active_version
        << ",\"hash\":\"" << JsonEscape(entry.chunk_hash) << "\""
        << ",\"level\":\"" << JsonEscape(entry.level) << "\""
        << ",\"compat\":\"" << JsonEscape(entry.compat_result) << "\""
        << ",\"actor\":\"" << JsonEscape(entry.actor) << "\""
        << "}";
    return oss.str();
}

} // namespace vm
} // namespace lpc
