#include "vm/runtime/debugger.h"
#include "vm/runtime/vm.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace lpc {
namespace vm {

static std::string Trim(const std::string &s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string Debugger::FormatValueEx(const Value &v, const Vm &vm) {
    if (v.IsObjRef()) {
        std::uintptr_t raw = v.AsObj();
        if (raw > 0 && raw < kFuncBase) {
            std::string s = vm.ResolveString(v);
            if (!s.empty()) return "\"" + s + "\"";
            return "<string>";
        }
        if (raw >= kArrayBase && raw < kMappingBase) {
            std::size_t sz = vm.GetArraySize(v);
            std::string result = "array[" + std::to_string(sz) + "]";
            std::size_t limit = (sz > 5) ? 5 : sz;
            result += " {";
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) result += ", ";
                Value elem = vm.GetArrayElement(v, static_cast<std::int64_t>(i));
                result += FormatValue(elem);
            }
            if (sz > 5) result += ", ...";
            result += "}";
            return result;
        }
        if (raw >= kMappingBase && raw < kClassBase) {
            std::size_t sz = vm.GetMappingSize(v);
            auto pairs = vm.GetMappingPairs(v);
            std::string result = "mapping[" + std::to_string(sz) + "]";
            std::size_t limit = (pairs.size() > 3) ? 3 : pairs.size();
            result += " {";
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) result += ", ";
                result += FormatValue(pairs[i].first) + ":" + FormatValue(pairs[i].second);
            }
            if (pairs.size() > 3) result += ", ...";
            result += "}";
            return result;
        }
        if (raw >= kClassBase && raw < kObjectBase) {
            const auto &names = vm.GetObjectFieldNames(v);
            std::string result = "class[" + std::to_string(names.size()) + "]";
            std::size_t limit = (names.size() > 3) ? 3 : names.size();
            result += " {";
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) result += ", ";
                result += names[i];
            }
            if (names.size() > 3) result += ", ...";
            result += "}";
            return result;
        }
        if (raw >= kObjectBase) {
            const auto &names = vm.GetObjectFieldNames(v);
            std::string result = "object[" + std::to_string(names.size()) + "]";
            std::size_t limit = (names.size() > 3) ? 3 : names.size();
            result += " {";
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) result += ", ";
                result += names[i];
            }
            if (names.size() > 3) result += ", ...";
            result += "}";
            return result;
        }
    }
    return FormatValue(v);
}

static std::vector<std::string> TokenizeExpr(const std::string &expr) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < expr.size()) {
        if (std::isspace(static_cast<unsigned char>(expr[i]))) {
            ++i;
            continue;
        }
        if (expr[i] == '.') {
            tokens.push_back(".");
            ++i;
            continue;
        }
        if (expr[i] == '[') {
            tokens.push_back("[");
            ++i;
            continue;
        }
        if (expr[i] == ']') {
            tokens.push_back("]");
            ++i;
            continue;
        }
        std::string tok;
        while (i < expr.size() && !std::isspace(static_cast<unsigned char>(expr[i]))
               && expr[i] != '.' && expr[i] != '[' && expr[i] != ']') {
            tok += expr[i];
            ++i;
        }
        if (!tok.empty()) tokens.push_back(tok);
    }
    return tokens;
}

Value Debugger::ResolveExpr(const std::string &expr,
                            const Chunk &chunk,
                            const std::vector<Frame> &frames,
                            const Value *stack,
                            std::size_t stack_size,
                            const Vm &vm) {
    std::string trimmed = Trim(expr);
    if (trimmed.empty()) return Value::Nil();

    auto tokens = TokenizeExpr(trimmed);
    if (tokens.empty()) return Value::Nil();

    Debugger dbg;
    Value current = dbg.ResolveVariable(tokens[0], chunk, frames, stack, stack_size);
    if (current.IsNil()) {
        current = ParseLiteral(tokens[0]);
    }
    if (current.IsNil()) return Value::Nil();

    std::size_t i = 1;
    while (i < tokens.size()) {
        if (tokens[i] == ".") {
            ++i;
            if (i >= tokens.size()) return Value::Nil();
            if (!current.IsObjRef()) return Value::Nil();
            current = vm.GetObjectField(current, tokens[i]);
            if (current.IsNil()) return Value::Nil();
            ++i;
        } else if (tokens[i] == "[") {
            ++i;
            if (i >= tokens.size()) return Value::Nil();
            std::string idx_str = tokens[i];
            ++i;
            if (i >= tokens.size() || tokens[i] != "]") return Value::Nil();
            ++i;

            if (current.IsObjRef()) {
                std::uintptr_t raw = current.AsObj();
                if (raw >= 0x100000000ULL && raw < 0x300000000ULL) {
                    try {
                        std::size_t pos = 0;
                        std::int64_t idx = std::stoll(idx_str, &pos, 0);
                        if (pos == idx_str.size()) {
                        current = vm.GetArrayElement(current, idx);
                        if (current.IsNil()) return Value::Nil();
                            continue;
                        }
                    } catch (...) {}
                }
                if (raw >= 0x200000000ULL && raw < 0x400000000ULL) {
                    Value key = ParseLiteral(idx_str);
                    if (key.IsNil()) {
                        try { key = Value::FromI64(std::stoll(idx_str)); }
                        catch (...) { return Value::Nil(); }
                    }
                    if (key.IsObjRef()) {
                        std::uintptr_t kraw = key.AsObj();
                        if (kraw > 0 && kraw < 0x080000000ULL) {
                            std::string s = vm.ResolveString(key);
                            Value skey = Value::FromObj(key.AsObj());
                            current = vm.GetMappingElement(current, skey);
                            if (current.IsNil()) return Value::Nil();
                            continue;
                        }
                    }
                    current = vm.GetMappingElement(current, key);
                    if (current.IsNil()) return Value::Nil();
                    continue;
                }
            }
            return Value::Nil();
        } else {
            return Value::Nil();
        }
    }

    return current;
}

} // namespace vm
} // namespace lpc
