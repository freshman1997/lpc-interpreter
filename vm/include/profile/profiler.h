#ifndef LPC_PROFILE_PROFILER_H
#define LPC_PROFILE_PROFILER_H

#include <cstdint>
#include <algorithm>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "opcode.h"

namespace lpc {
namespace profile {

class Profiler {
public:
    void CountOp(OpCode op) {
        ++op_counts_[static_cast<int>(op)];
    }

    std::uint64_t GetCount(OpCode op) const {
        auto it = op_counts_.find(static_cast<int>(op));
        if (it == op_counts_.end()) {
            return 0;
        }
        return it->second;
    }

    void CountFunction(const std::string &name) {
        ++func_counts_[name];
    }

    std::string DumpJson() const {
        auto op_items = ToSortedVector(op_counts_);
        auto fn_items = ToSortedVector(func_counts_);

        std::string out = "{\"opcodes\":[";
        bool first = true;
        for (const auto &it : op_items) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += "{\"opcode\":" + std::to_string(it.first) + ",\"count\":" + std::to_string(it.second) + "}";
        }
        out += "],\"functions\":[";
        first = true;
        for (const auto &it : fn_items) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += "{\"name\":\"" + EscapeJson(it.first) + "\",\"count\":" + std::to_string(it.second) + "}";
        }
        out += "]}";
        return out;
    }

private:
    static std::string EscapeJson(const std::string &in) {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if (c == '\\' || c == '"') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out;
    }

    template<typename K>
    static std::vector<std::pair<K, std::uint64_t>> ToSortedVector(const std::unordered_map<K, std::uint64_t> &src) {
        std::vector<std::pair<K, std::uint64_t>> out;
        out.reserve(src.size());
        for (const auto &it : src) {
            out.push_back(it);
        }

        std::sort(out.begin(), out.end(), [](const std::pair<K, std::uint64_t> &a, const std::pair<K, std::uint64_t> &b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });
        return out;
    }

    std::unordered_map<int, std::uint64_t> op_counts_;
    std::unordered_map<std::string, std::uint64_t> func_counts_;
};

} // namespace profile
} // namespace lpc

#endif
