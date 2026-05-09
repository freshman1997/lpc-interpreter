#include "vm/runtime/vm.h"
#include "vm/value/objref.h"
#include "vm/value/mapping.h"
#include "vm/value/lpc_array.h"
#include "vm/value/hash.h"
#include "vm/runtime/efun.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <cmath>
#include <ctime>
#include <array>
#include <algorithm>
#include <thread>
#include <chrono>
#include <random>
#include <regex>
#include <cctype>
#include <list>

using namespace lpc::vm;

static std::string FormatValue(const Value &v, Vm &vm, bool nested = false, int depth = 0) {
    const Chunk &chunk = vm.BoundChunk();
    const std::vector<std::string> &string_heap = vm.string_heap();
    if (depth > static_cast<int>(kFormatMaxDepth)) return "...";

    switch (v.Tag()) {
    case ValueTag::Nil: return "0";
    case ValueTag::Bool: return v.AsBool() ? "1" : "0";
    case ValueTag::Int64: return std::to_string(vm.GetI64(v));
    case ValueTag::BoxedInt: return std::to_string(vm.GetI64(v));
    case ValueTag::Float64: {
        std::ostringstream oss;
        oss << v.AsF64();
        return oss.str();
    }
    case ValueTag::ObjRef: {
        std::uintptr_t raw = v.AsObj();
        if (raw > 0 && raw < kFuncBase) {
            if (IsSConstStringRaw(raw)) {
                std::uint32_t sidx = DecodeStringIndex(raw);
                if (sidx < chunk.sconst.size()) {
                    return nested ? ("\"" + chunk.sconst[sidx] + "\"") : chunk.sconst[sidx];
                }
            } else {
                std::uint32_t hidx = DecodeStringIndex(raw);
                if (hidx < string_heap.size()) {
                    return nested ? ("\"" + string_heap[hidx] + "\"") : string_heap[hidx];
                }
            }
        }
        if (raw >= kArrayBase && raw < kMappingBase) {
            std::size_t sz = vm.GetArraySize(v);
            std::string out = "({";
            for (std::size_t i = 0; i < sz; ++i) {
                if (i > 0) out += ", ";
                out += FormatValue(vm.GetArrayElement(v, static_cast<std::int64_t>(i)), vm, true, depth + 1);
            }
            out += "})";
            return out;
        }
        if (raw >= kMappingBase && raw < kClassBase) {
            auto pairs = vm.GetMappingPairs(v);
            std::string out = "{";
            for (std::size_t i = 0; i < pairs.size(); ++i) {
                if (i > 0) out += ", ";
                out += FormatValue(pairs[i].first, vm, true, depth + 1);
                out += ": ";
                out += FormatValue(pairs[i].second, vm, true, depth + 1);
            }
            out += "}";
            return out;
        }
        if (raw >= kClassBase && raw < kObjectBase) {
            const auto &names = vm.GetObjectFieldNames(v);
            std::string out = "class {";
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) out += ", ";
                out += names[i];
                out += ": ";
                out += FormatValue(vm.GetObjectField(v, names[i]), vm, true, depth + 1);
            }
            out += "}";
            return out;
        }
        if (raw >= kObjectBase) return "<object:" + std::to_string(DecodeObjectId(v)) + ">";
        return "<ref>";
    }
    case ValueTag::Closure: return "<closure>";
    }
    return "<unknown>";
}

static std::string FormatCtimeSafe(std::time_t t) {
    std::array<char, 64> buf{};
#if defined(_WIN32)
    if (ctime_s(buf.data(), buf.size(), &t) != 0) return "";
    std::string s(buf.data());
#elif defined(__unix__) || defined(__APPLE__)
    if (ctime_r(&t, buf.data()) == nullptr) return "";
    std::string s(buf.data());
#else
    char *ct = std::ctime(&t);
    std::string s(ct ? ct : "");
#endif
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static void IntrinsicPrint(Vm &vm, const std::vector<Value> &args, const Chunk &chunk,
                            const std::vector<std::string> &string_heap) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += " ";
        out += FormatValue(args[i], vm);
    }
    out += "\n";
    vm.EmitOutput(out);
}

static void IntrinsicPuts(Vm &vm, const std::vector<Value> &args, const Chunk &chunk,
                           const std::vector<std::string> &string_heap) {
    if (!args.empty()) {
        vm.EmitOutput(FormatValue(args[0], vm) + "\n");
    }
}

static LpcArray *GetArrayObjRef(const Value &v,
                                                std::vector<LpcArray> &arrays_) {
    std::size_t arr_id = DecodeArrayId(v);
    if (arr_id == 0 || arr_id > arrays_.size()) return nullptr;
    return &arrays_[arr_id - 1];
}

static Mapping *GetMappingObjRef(
    const Value &v,
    std::vector<Mapping> &mappings_) {
    std::size_t map_id = DecodeMappingId(v);
    if (map_id == 0 || map_id > mappings_.size()) return nullptr;
    return &mappings_[map_id - 1];
}

static std::regex_constants::syntax_option_type RegexFlagsFromArg(const Value *arg, Vm &vm) {
    std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript;
    if (!arg || !arg->IsObjRef()) return flags;
    const std::string text = vm.ResolveObjRefStringOnly(*arg);
    for (char ch : text) {
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(ch)))) {
        case 'i':
            flags |= std::regex_constants::icase;
            break;
        case 'n':
            flags |= std::regex_constants::nosubs;
            break;
        case 'o':
            flags |= std::regex_constants::optimize;
            break;
        case 'm':
        case 's':
            // Reserved for future behavior extensions.
            break;
        default:
            break;
        }
    }
    return flags;
}

static std::string RegexPatternDotAll(const std::string &pattern) {
    std::string out;
    out.reserve(pattern.size() + 8);
    bool in_class = false;
    bool escaped = false;
    for (char ch : pattern) {
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            out.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '[') {
            in_class = true;
            out.push_back(ch);
            continue;
        }
        if (ch == ']') {
            in_class = false;
            out.push_back(ch);
            continue;
        }
        if (!in_class && ch == '.') {
            out.append("[\\s\\S]");
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

static bool RegexFlagEnabled(const Value *arg, Vm &vm, char needle) {
    if (!arg || !arg->IsObjRef()) return false;
    const std::string text = vm.ResolveObjRefStringOnly(*arg);
    for (char ch : text) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(ch))) == needle) {
            return true;
        }
    }
    return false;
}

static bool RegexFlagsValid(const Value *arg, Vm &vm) {
    if (!arg || !arg->IsObjRef()) return true;
    const std::string text = vm.ResolveObjRefStringOnly(*arg);
    for (char ch : text) {
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(ch)))) {
        case 'i':
        case 'm':
        case 's':
        case 'n':
        case 'o':
            break;
        default:
            return false;
        }
    }
    return true;
}

struct RegexCacheState {
    using CacheNode = std::pair<std::string, std::regex>;
    std::list<CacheNode> lru;
    std::unordered_map<std::string, std::list<CacheNode>::iterator> index;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
};

static RegexCacheState &RegexCache() {
    static RegexCacheState state;
    return state;
}

static const std::regex *GetOrCompileRegexCached(
    const std::string &pattern,
    std::regex_constants::syntax_option_type syntax) {
    static constexpr std::size_t kRegexCacheCap = 128;
    RegexCacheState &cache = RegexCache();

    const std::string key = pattern + "\n" + std::to_string(static_cast<int>(syntax));
    auto it = cache.index.find(key);
    if (it != cache.index.end()) {
        ++cache.hits;
        cache.lru.splice(cache.lru.begin(), cache.lru, it->second);
        return &cache.lru.begin()->second;
    }

    ++cache.misses;

    std::regex compiled(pattern, syntax);
    cache.lru.emplace_front(key, std::move(compiled));
    cache.index[key] = cache.lru.begin();

    if (cache.lru.size() > kRegexCacheCap) {
        auto last = cache.lru.end();
        --last;
        cache.index.erase(last->first);
        cache.lru.pop_back();
        ++cache.evictions;
    }
    return &cache.lru.begin()->second;
}

static void GetRegexCacheStats(std::uint64_t *hits, std::uint64_t *misses, std::uint64_t *evictions) {
    RegexCacheState &cache = RegexCache();
    if (hits) *hits = cache.hits;
    if (misses) *misses = cache.misses;
    if (evictions) *evictions = cache.evictions;
}

Value Vm::DispatchIntrinsic(std::uint16_t efun_idx, std::uint8_t argc) {
    if (argc > StackSize()) {
        return Value::Nil();
    }

    const Efun efun = static_cast<Efun>(efun_idx);
    if (efun == Efun::ThisObject && argc == 0) {
        if (current_object_id_ > 0 && current_object_id_ <= objects_.size()) {
            return MakeObjectHandle(current_object_id_);
        }
        return Value::Nil();
    }
    if (efun == Efun::Typeof && argc == 1) {
        Value v = StackPop();
        return MakeI64(static_cast<std::int64_t>(v.Tag()));
    }
    if (efun == Efun::ToInt && argc == 1) {
        Value v = StackPop();
        if (v.IsBool()) return MakeI64(v.AsI64());
        if (v.Tag() == ValueTag::Int64) return v;
        if (v.IsFloat64()) return MakeI64(static_cast<std::int64_t>(v.AsF64()));
        if (v.IsNil()) return MakeI64(0);
        return MakeI64(0);
    }
    if (efun == Efun::ToString && argc == 1) {
        Value v = StackPop();
        std::string s;
        if (v.Tag() == ValueTag::Int64) {
            s = std::to_string(GetI64(v));
        } else if (v.IsBool()) {
            s = v.AsI64() ? "1" : "0";
        } else if (v.IsFloat64()) {
            s = std::to_string(v.AsF64());
        } else if (v.IsNil()) {
            s = "0";
        } else if (v.IsObjRef()) {
            s = ResolveObjRefStringOnly(v);
        }
        if (!s.empty()) {
            return InternString(s);
        }
        return Value::Nil();
    }
    if (efun == Efun::Instanceof && argc == 2) {
        Value rhs = StackPop();
        Value lhs = StackPop();
        if (!IsClassObjRef(lhs)) return MakeI64(0);
        std::uint16_t cur = 0;
        if (!ResolveClassTemplateIndex(lhs, &cur)) return MakeI64(0);
        std::string target_name;
        if (rhs.IsObjRef() && IsStringObjRefFull(rhs)) {
            target_name = ResolveObjRefStringOnly(rhs);
        } else if (rhs.Tag() == ValueTag::Int64) {
            std::uint16_t idx = static_cast<std::uint16_t>(GetI64(rhs));
            if (idx < BoundChunk().classes.size()) target_name = BoundChunk().classes[idx].name;
        }
        if (target_name.empty()) return MakeI64(0);
        while (cur != kInvalidIndex16 && cur < BoundChunk().classes.size()) {
            if (BoundChunk().classes[cur].name == target_name) return MakeI64(1);
            cur = BoundChunk().classes[cur].parent_class_idx;
        }
        return MakeI64(0);
    }

    thread_local std::vector<Value> cached_args;
    cached_args.resize(argc);
    for (std::uint8_t i = argc; i > 0; --i) {
        cached_args[i - 1] = StackPop();
    }
    const std::vector<Value> &args = cached_args;

    switch (efun) {
    case Efun::CallOther: {
        return Value::Nil();
    }
    case Efun::CallLater: {
        if (args.size() < 2 || args[0].Tag() != ValueTag::Int64) {
            return MakeI64(0);
        }
        const Value &callee = args[1];
        if (!(IsFuncObjRef(callee) || callee.IsClosure() || IsStringObjRefFull(callee))) {
            return MakeI64(0);
        }

        const std::int64_t delay_ms = GetI64(args[0]) < 0 ? 0 : GetI64(args[0]);
        static constexpr std::size_t kMaxPendingTimers = 4096;
        static constexpr std::size_t kMaxPendingTimersPerModule = 1024;
        if (timer_pending_count_ >= kMaxPendingTimers) {
            return MakeI64(0);
        }
        const std::uint64_t module_pending_count = module_timer_pending_count_[current_module_name_];
        if (module_pending_count >= kMaxPendingTimersPerModule) {
            return MakeI64(0);
        }
        TimerTask task;
        task.id = next_timer_id_++;
        if (task.id == 0) {
            task.id = next_timer_id_++;
        }
        task.due_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        task.callee = callee;
        for (std::size_t i = 2; i < args.size(); ++i) {
            task.args.push_back(args[i]);
        }
        task.module_name = current_module_name_;
        task.module_version_id = current_module_version_id_;
        task.object_id = current_object_id_;
        timers_.push_back(std::move(task));
        timer_id_index_[timers_.back().id] = timers_.size() - 1;
        timer_heap_.push(TimerHeapEntry{timers_.back().due_at, timers_.back().id});
        ++timer_created_count_;
        ++timer_pending_count_;
        ++module_timer_pending_count_[timers_.back().module_name];
        if (timer_pending_count_ > timer_pending_peak_) {
            timer_pending_peak_ = timer_pending_count_;
        }
        if (timers_.back().due_at < timer_next_due_) {
            timer_next_due_ = timers_.back().due_at;
        }
        return MakeI64(static_cast<std::int64_t>(timers_.back().id));
    }
    case Efun::CancelTimer: {
        if (args.empty() || args[0].Tag() != ValueTag::Int64) {
            return MakeI64(0);
        }
        const std::uint64_t timer_id = static_cast<std::uint64_t>(GetI64(args[0]));
        auto it = timer_id_index_.find(timer_id);
        if (it != timer_id_index_.end() && it->second < timers_.size()) {
            auto &timer = timers_[it->second];
            if (timer.id != timer_id || timer.cancelled) {
                return MakeI64(0);
            }
            timer.cancelled = true;
            timer.args.clear();
            timer.callee = Value::Nil();
            timer_id_index_.erase(it);
            ++timer_cancelled_count_;
            if (timer_pending_count_ > 0) {
                --timer_pending_count_;
            }
            auto mit = module_timer_pending_count_.find(timer.module_name);
            if (mit != module_timer_pending_count_.end()) {
                if (mit->second > 1) {
                    --mit->second;
                } else {
                    module_timer_pending_count_.erase(mit);
                }
            }
            return MakeI64(1);
        }
        return MakeI64(0);
    }

    if (argc == 1) {
        const Value v = StackBack();
        switch (efun) {
        case Efun::Stringp:
            StackPop();
            return MakeBool(IsStringObjRefFull(v));
        case Efun::Intp:
            StackPop();
            return MakeBool(v.Tag() == ValueTag::Int64);
        case Efun::Floatp:
            StackPop();
            return MakeBool(v.IsFloat64());
        case Efun::Nullp:
            StackPop();
            return MakeBool(v.IsNil());
        case Efun::Functionp:
            StackPop();
            return MakeBool(v.IsClosure());
        default:
            break;
        }
    }
    case Efun::TimerExists: {
        if (args.empty() || args[0].Tag() != ValueTag::Int64) {
            return MakeI64(0);
        }
        const std::uint64_t timer_id = static_cast<std::uint64_t>(GetI64(args[0]));
        auto it = timer_id_index_.find(timer_id);
        if (it != timer_id_index_.end() && it->second < timers_.size()) {
            const auto &timer = timers_[it->second];
            if (timer.id == timer_id && !timer.cancelled) return MakeI64(1);
        }
        return MakeI64(0);
    }
    case Efun::PendingTimers: {
        return MakeI64(static_cast<std::int64_t>(timer_pending_count_));
    }
    case Efun::TimerInfo: {
        if (args.empty() || args[0].Tag() != ValueTag::Int64) {
            return Value::Nil();
        }
        const std::uint64_t timer_id = static_cast<std::uint64_t>(GetI64(args[0]));
        auto it = timer_id_index_.find(timer_id);
        if (it != timer_id_index_.end() && it->second < timers_.size()) {
            const auto &timer = timers_[it->second];
            if (timer.id != timer_id || timer.cancelled) {
                return Value::Nil();
            }
            Mapping info;
            info.Insert(InternString("id"), MakeI64(static_cast<std::int64_t>(timer.id)));
            info.Insert(InternString("exists"), MakeI64(1));
            const auto now = std::chrono::steady_clock::now();
            const auto due_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timer.due_at - now).count();
            info.Insert(InternString("due_ms"), MakeI64(due_ms > 0 ? due_ms : 0));
            info.Insert(InternString("module"), InternString(timer.module_name));
            info.Insert(InternString("cancelled"), MakeI64(timer.cancelled ? 1 : 0));
            info.Insert(InternString("argc"), MakeI64(static_cast<std::int64_t>(timer.args.size())));
            if (timer.callee.IsClosure()) {
                info.Insert(InternString("callback_kind"), InternString("closure"));
                const std::uint32_t cid = timer.callee.ClosureId();
                if (cid > 0 && cid <= closures_.size()) {
                    const std::uint16_t fid = closures_[cid - 1].FuncId();
                    if (fid < BoundChunk().functions.size()) {
                        info.Insert(InternString("callback_name"), InternString(BoundChunk().functions[fid].name));
                    }
                }
            } else if (IsFuncObjRef(timer.callee)) {
                info.Insert(InternString("callback_kind"), InternString("function"));
                const std::size_t raw_fid = DecodeFuncId(timer.callee);
                if (raw_fid > 0) {
                    const std::size_t fid = raw_fid - 1;
                    if (fid < BoundChunk().functions.size()) {
                        info.Insert(InternString("callback_name"), InternString(BoundChunk().functions[fid].name));
                    }
                }
            } else if (IsStringObjRefFull(timer.callee)) {
                info.Insert(InternString("callback_kind"), InternString("string"));
                info.Insert(InternString("callback_name"), InternString(ResolveObjRefStringOnly(timer.callee)));
            } else {
                info.Insert(InternString("callback_kind"), InternString("unknown"));
                info.Insert(InternString("callback_name"), InternString(""));
            }
            return AllocateMappingHandle(std::move(info));
        }
        return Value::Nil();
    }
    case Efun::TimerClearModule: {
        std::string target_module = current_module_name_;
        if (!args.empty() && args[0].IsObjRef()) {
            target_module = ResolveObjRefStringOnly(args[0]);
        }
        if (target_module.empty()) {
            return MakeI64(0);
        }
        std::int64_t removed = 0;
        for (std::size_t i = 0; i < timers_.size(); ++i) {
            auto &timer = timers_[i];
            if (timer.cancelled) continue;
            if (timer.module_name != target_module) continue;
            timer.cancelled = true;
            timer_id_index_.erase(timer.id);
            ++removed;
        }
        if (removed > 0) {
            if (static_cast<std::uint64_t>(removed) >= timer_pending_count_) {
                timer_pending_count_ = 0;
            } else {
                timer_pending_count_ -= static_cast<std::uint64_t>(removed);
            }
            auto mit = module_timer_pending_count_.find(target_module);
            if (mit != module_timer_pending_count_.end()) {
                if (static_cast<std::uint64_t>(removed) >= mit->second) {
                    module_timer_pending_count_.erase(mit);
                } else {
                    mit->second -= static_cast<std::uint64_t>(removed);
                }
            }
        }
        timer_cleared_count_ += static_cast<std::uint64_t>(removed);
        return MakeI64(removed);
    }
    case Efun::TimerStats: {
        Mapping stats;
        stats.Insert(InternString("created"), MakeI64(static_cast<std::int64_t>(timer_created_count_)));
        stats.Insert(InternString("fired"), MakeI64(static_cast<std::int64_t>(timer_fired_count_)));
        stats.Insert(InternString("cancelled"), MakeI64(static_cast<std::int64_t>(timer_cancelled_count_)));
        stats.Insert(InternString("cleared"), MakeI64(static_cast<std::int64_t>(timer_cleared_count_)));
        stats.Insert(InternString("pending"), MakeI64(static_cast<std::int64_t>(timer_pending_count_)));
        stats.Insert(InternString("pending_peak"), MakeI64(static_cast<std::int64_t>(timer_pending_peak_)));
        stats.Insert(InternString("heap_size"), MakeI64(static_cast<std::int64_t>(timer_heap_.size())));
        stats.Insert(InternString("heap_stale_pops"), MakeI64(static_cast<std::int64_t>(timer_heap_stale_pops_)));
        stats.Insert(InternString("heap_rebuilds"), MakeI64(static_cast<std::int64_t>(timer_heap_rebuild_count_)));
        return AllocateMappingHandle(std::move(stats));
    }
    case Efun::RegexStats: {
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t evictions = 0;
        GetRegexCacheStats(&hits, &misses, &evictions);
        Mapping stats;
        stats.Insert(InternString("hits"), MakeI64(static_cast<std::int64_t>(hits)));
        stats.Insert(InternString("misses"), MakeI64(static_cast<std::int64_t>(misses)));
        stats.Insert(InternString("evictions"), MakeI64(static_cast<std::int64_t>(evictions)));
        return AllocateMappingHandle(std::move(stats));
    }
    case Efun::Regexp: {
        if (args.size() < 2 || !args[0].IsObjRef() || !args[1].IsObjRef()) {
            return MakeI64(0);
        }
        const std::string text = ResolveObjRefStringOnly(args[0]);
        std::string pattern = ResolveObjRefStringOnly(args[1]);
        const Value *flags_arg = args.size() >= 3 ? &args[2] : nullptr;
        if (!RegexFlagsValid(flags_arg, *this)) {
            return MakeI64(0);
        }
        if (RegexFlagEnabled(flags_arg, *this, 's')) {
            pattern = RegexPatternDotAll(pattern);
        }
        try {
#if defined(__cpp_lib_regex)
            auto syntax = RegexFlagsFromArg(flags_arg, *this);
            if (RegexFlagEnabled(flags_arg, *this, 'm')) {
                syntax |= std::regex_constants::multiline;
            }
            const std::regex *re = GetOrCompileRegexCached(pattern, syntax);
#else
            auto syntax = RegexFlagsFromArg(flags_arg, *this);
            const std::regex *re = GetOrCompileRegexCached(pattern, syntax);
#endif
            return MakeI64(std::regex_search(text, *re) ? 1 : 0);
        } catch (...) {
            return MakeI64(0);
        }
    }
    case Efun::RegexReplace: {
        if (args.size() < 3 || !args[0].IsObjRef() || !args[1].IsObjRef() || !args[2].IsObjRef()) {
            return Value::Nil();
        }
        const std::string text = ResolveObjRefStringOnly(args[0]);
        std::string pattern = ResolveObjRefStringOnly(args[1]);
        const std::string repl = ResolveObjRefStringOnly(args[2]);
        const Value *flags_arg = args.size() >= 4 ? &args[3] : nullptr;
        if (!RegexFlagsValid(flags_arg, *this)) {
            return Value::Nil();
        }
        if (RegexFlagEnabled(flags_arg, *this, 's')) {
            pattern = RegexPatternDotAll(pattern);
        }
        try {
#if defined(__cpp_lib_regex)
            auto syntax = RegexFlagsFromArg(flags_arg, *this);
            if (RegexFlagEnabled(flags_arg, *this, 'm')) {
                syntax |= std::regex_constants::multiline;
            }
            const std::regex *re = GetOrCompileRegexCached(pattern, syntax);
#else
            auto syntax = RegexFlagsFromArg(flags_arg, *this);
            const std::regex *re = GetOrCompileRegexCached(pattern, syntax);
#endif
            return InternString(std::regex_replace(text, *re, repl));
        } catch (...) {
            return Value::Nil();
        }
    }
    case Efun::Getenv: {
        Mapping env;
        for (const auto &item : env_params_) {
            env.Insert(InternString(item.first), InternString(item.second));
        }
        if (!args.empty()) {
            std::string key = ResolveObjRefStringOnly(args[0]);
            Value *found = env.Find(InternString(key));
            return found ? *found : Value::Nil();
        }
        return AllocateMappingHandle(std::move(env));
    }
    case Efun::Print: IntrinsicPrint(*this, args, BoundChunk(), string_heap_); break;
    case Efun::Puts: IntrinsicPuts(*this, args, BoundChunk(), string_heap_); break;
    case Efun::Sleep: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) {
            std::this_thread::sleep_for(std::chrono::milliseconds(GetI64(args[0])));
        }
        return Value::Nil();
    }
    case Efun::Sizeof: {
        if (!args.empty()) {
            if (args[0].IsObjRef()) {
                if (IsStringObjRefFull(args[0])) {
                    std::string s = ResolveObjRefStringOnly(args[0]);
                    return MakeI64(static_cast<std::int64_t>(s.size()));
                }
                auto *arr = GetArrayObjRef(args[0], arrays_);
                if (arr) return MakeI64(static_cast<std::int64_t>(arr->Size()));
                auto *map = GetMappingObjRef(args[0], mappings_);
                if (map) return MakeI64(static_cast<std::int64_t>(map->Size()));
            }
            if (args[0].Tag() == ValueTag::Int64) {
                return MakeI64(GetI64(args[0]) == 0 ? 0 : 1);
            }
        }
        return MakeI64(0);
    }
    case Efun::Random: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64 && GetI64(args[0]) > 0) {
            std::int64_t n = GetI64(args[0]);
            thread_local std::mt19937_64 rng{static_cast<std::uint64_t>(std::time(nullptr))};
            std::uniform_int_distribution<std::int64_t> dist(0, n - 1);
            return MakeI64(dist(rng));
        }
        return MakeI64(0);
    }
    case Efun::Keys: {
        if (!args.empty()) {
            auto *map = GetMappingObjRef(args[0], mappings_);
            if (map) {
                std::vector<Value> keys;
                map->ForEachEntry([&](const Value &k, const Value &) {
                    keys.push_back(k);
                });
                return AllocateArrayHandle(std::move(keys));
            }
        }
        return Value::Nil();
    }
    case Efun::Values: {
        if (!args.empty()) {
            auto *map = GetMappingObjRef(args[0], mappings_);
            if (map) {
                std::vector<Value> vals;
                map->ForEachEntry([&](const Value &, const Value &v) {
                    vals.push_back(v);
                });
                return AllocateArrayHandle(std::move(vals));
            }
        }
        return Value::Nil();
    }
    case Efun::Typeof: {
        if (!args.empty()) {
            return MakeI64(static_cast<std::int64_t>(args[0].Tag()));
        }
        return MakeI64(0);
    }
    case Efun::ToString: {
        if (!args.empty()) {
            std::string s;
            if (args[0].Tag() == ValueTag::Int64) {
                s = std::to_string(GetI64(args[0]));
            } else if (args[0].IsBool()) {
                s = args[0].AsI64() ? "1" : "0";
            } else if (args[0].IsFloat64()) {
                s = std::to_string(args[0].AsF64());
            } else if (args[0].IsNil()) {
                s = "0";
            } else if (args[0].IsObjRef()) {
                s = ResolveObjRefStringOnly(args[0]);
            }
            if (!s.empty()) {
                return InternString(s);
            }
        }
        return Value::Nil();
    }
    case Efun::ToInt: {
        if (!args.empty()) {
            if (args[0].IsBool()) return MakeI64(args[0].AsI64());
            if (args[0].Tag() == ValueTag::Int64) return args[0];
            if (args[0].IsFloat64())
                return MakeI64(static_cast<std::int64_t>(args[0].AsF64()));
            if (args[0].IsNil()) return MakeI64(0);
        }
        return MakeI64(0);
    }
    case Efun::ThisObject: {
        if (current_object_id_ > 0 && current_object_id_ <= objects_.size()) {
            return MakeObjectHandle(current_object_id_);
        }
        return Value::Nil();
    }
    case Efun::CloneObject: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string path = ResolveObjRefStringOnly(args[0]);
            Vm::LpcObject obj;
            obj.module_name = path;
            const Chunk *obj_chunk = nullptr;
            std::uint64_t obj_version = 0;
            std::string obj_module_name = path;
            const ModuleRuntimeState *tms = GetModuleState(path);
            if (tms && tms->active_version_id != 0) {
                auto vit = tms->version_runtime_data.find(tms->active_version_id);
                if (vit != tms->version_runtime_data.end()) {
                    obj.module_version_id = tms->active_version_id;
                    obj.blueprint = &vit->second.chunk;
                    obj.globals = vit->second.chunk.globals;
                    obj.destroyed = false;
                    obj_chunk = &vit->second.chunk;
                    obj_version = tms->active_version_id;
                    Value handle = AllocateObjectHandle(std::move(obj));
                    if (obj_chunk && obj_chunk->create_idx != kInvalidIndex16) {
                        PendingLifecycleCall plc;
                        plc.func_id = obj_chunk->create_idx;
                        plc.object_id = DecodeObjectId(handle);
                        plc.module_version_id = obj_version;
                        plc.module_name = obj_module_name;
                        pending_lifecycle_.push_back(std::move(plc));
                    }
                    return handle;
                }
            }
            obj.module_version_id = current_module_version_id_;
            obj.blueprint = &BoundChunk();
            obj.globals = BoundChunk().globals;
            obj.destroyed = false;
            obj_chunk = &BoundChunk();
            obj_version = current_module_version_id_;
            Value handle = AllocateObjectHandle(std::move(obj));
            if (obj_chunk && obj_chunk->create_idx != kInvalidIndex16) {
                PendingLifecycleCall plc;
                plc.func_id = obj_chunk->create_idx;
                plc.object_id = DecodeObjectId(handle);
                plc.module_version_id = obj_version;
                plc.module_name = obj_module_name;
                pending_lifecycle_.push_back(std::move(plc));
            }
            return handle;
        }
        return Value::Nil();
    }
    case Efun::Destruct: {
        if (!args.empty()) {
            std::size_t oid = DecodeObjectId(args[0]);
            if (oid > 0 && oid <= objects_.size()) {
                const LpcObject &dobj = objects_[oid - 1];
                if (!dobj.destroyed && dobj.blueprint && dobj.blueprint->on_destruct_idx != kInvalidIndex16) {
                    PendingLifecycleCall plc;
                    plc.func_id = dobj.blueprint->on_destruct_idx;
                    plc.object_id = static_cast<std::uint32_t>(oid);
                    plc.module_version_id = dobj.module_version_id;
                    plc.module_name = dobj.module_name;
                    pending_lifecycle_.push_back(std::move(plc));
                }
                objects_[oid - 1].destroyed = true;
            }
        }
        return Value::Nil();
    }
    case Efun::Sprintf: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string fmt = ResolveObjRefStringOnly(args[0]);
            std::string buf;
            int arg_idx = 1;
            for (std::size_t p = 0; p < fmt.size(); ++p) {
                if (fmt[p] != '%') { buf.push_back(fmt[p]); continue; }
                ++p;
                if (p >= fmt.size()) break;
                if (fmt[p] == '%') { buf.push_back('%'); continue; }
                if (arg_idx >= static_cast<int>(args.size())) { buf.push_back('%'); buf.push_back(fmt[p]); continue; }
                const Value &arg = args[arg_idx];
                ++arg_idx;
                switch (fmt[p]) {
                case 'd': case 'i':
                    if (arg.Tag() == ValueTag::Int64) buf.append(std::to_string(GetI64(arg)));
                    else buf.append("0");
                    break;
                case 'f':
                    if (arg.IsFloat64()) {
                        char tmp[64]; std::snprintf(tmp, sizeof(tmp), "%g", arg.AsF64()); buf.append(tmp);
                    } else if (arg.Tag() == ValueTag::Int64) {
                        char tmp[64]; std::snprintf(tmp, sizeof(tmp), "%g", static_cast<double>(GetI64(arg))); buf.append(tmp);
                    } else buf.append("0");
                    break;
                case 's':
                    buf.append(FormatValue(arg, *this));
                    break;
                default:
                    buf.push_back('%'); buf.push_back(fmt[p]); --arg_idx; break;
                }
            }
            if (!buf.empty()) return InternString(buf);
        }
        return Value::Nil();
    }
    case Efun::Write: {
        if (!args.empty()) {
            EmitOutput(FormatValue(args[0], *this));
        }
        return Value::Nil();
    }
    case Efun::Time: {
        return MakeI64(static_cast<std::int64_t>(std::time(nullptr)));
    }
    case Efun::MemberArray: {
        if (args.size() >= 2) {
            auto *arr = GetArrayObjRef(args[1], arrays_);
            if (arr) {
                for (std::size_t i = 0; i < arr->Size(); ++i) {
                    if (ValuesEqual(args[0], arr->At(i))) return MakeI64(static_cast<std::int64_t>(i));
                }
                return MakeI64(-1);
            }
            if (args[1].IsObjRef() && args[0].IsObjRef()) {
                std::string haystack = ResolveObjRefStringOnly(args[1]);
                std::string needle = ResolveObjRefStringOnly(args[0]);
                std::size_t pos = haystack.find(needle);
                if (pos == std::string::npos) return MakeI64(-1);
                return MakeI64(static_cast<std::int64_t>(pos));
            }
        }
        return MakeI64(-1);
    }
    case Efun::Explode: {
        if (args.size() >= 2 && args[0].IsObjRef() && args[1].IsObjRef()) {
            std::string str = ResolveObjRefStringOnly(args[0]);
            std::string delim = ResolveObjRefStringOnly(args[1]);
            std::vector<Value> parts;
            int dlen = static_cast<int>(delim.size());
            if (dlen == 0) {
                for (char c : str) {
                    std::string ch(1, c);
                    parts.push_back(InternString(ch));
                }
            } else {
                std::size_t pos = 0;
                while (pos < str.size()) {
                    std::size_t found = str.find(delim, pos);
                    if (found == std::string::npos) {
                        parts.push_back(InternString(str.substr(pos)));
                        break;
                    }
                    parts.push_back(InternString(str.substr(pos, found - pos)));
                    pos = found + dlen;
                }
                if (pos == str.size()) {
                    parts.push_back(InternString(""));
                }
            }
            return AllocateArrayHandle(std::move(parts));
        }
        return Value::Nil();
    }
    case Efun::Implode: {
        if (args.size() >= 2) {
            auto *arr = GetArrayObjRef(args[0], arrays_);
            if (arr) {
                std::string d;
                if (args[1].IsObjRef()) d = ResolveObjRefStringOnly(args[1]);
                std::string buf;
                for (std::size_t i = 0; i < arr->Size(); ++i) {
                    const Value &elem = arr->At(i);
                    if (elem.IsObjRef()) {
                        buf.append(ResolveObjRefStringOnly(elem));
                    } else if (elem.Tag() == ValueTag::Int64) {
                        buf.append(std::to_string(GetI64(elem)));
                    } else if (elem.IsFloat64()) {
                        buf.append(std::to_string(elem.AsF64()));
                    }
                    if (i + 1 < arr->Size()) buf.append(d);
                }
                if (!buf.empty()) return InternString(buf);
            }
        }
        return Value::Nil();
    }
    case Efun::Stringp: {
        if (!args.empty()) return MakeBool(IsStringObjRefFull(args[0]));
        return MakeBool(false);
    }
    case Efun::Intp: {
        if (!args.empty()) return MakeBool(args[0].Tag() == ValueTag::Int64);
        return MakeBool(false);
    }
    case Efun::Floatp: {
        if (!args.empty()) return MakeBool(args[0].IsFloat64());
        return MakeBool(false);
    }
    case Efun::Arrayp: {
        if (!args.empty()) {
            if (args[0].IsObjRef()) {
                if (IsStringObjRefFull(args[0])) return MakeBool(false);
                return MakeBool(GetArrayObjRef(args[0], arrays_) != nullptr);
            }
        }
        return MakeBool(false);
    }
    case Efun::Mappingp: {
        if (!args.empty()) {
            if (args[0].IsObjRef()) {
                if (IsStringObjRefFull(args[0])) return MakeBool(false);
                if (GetArrayObjRef(args[0], arrays_)) return MakeBool(false);
                return MakeBool(GetMappingObjRef(args[0], mappings_) != nullptr);
            }
        }
        return MakeBool(false);
    }
    case Efun::Objectp: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::size_t oid = DecodeObjectId(args[0]);
            if (oid > 0 && oid <= objects_.size() && !objects_[oid - 1].destroyed) {
                return MakeBool(true);
            }
        }
        return MakeBool(false);
    }
    case Efun::Nullp: {
        if (!args.empty()) return MakeBool(args[0].IsNil());
        return MakeBool(true);
    }
    case Efun::Functionp: {
        if (!args.empty()) return MakeBool(args[0].IsClosure());
        return MakeBool(false);
    }
    case Efun::ToFloat: {
        if (!args.empty()) {
            if (args[0].IsFloat64()) return args[0];
            if (args[0].Tag() == ValueTag::Int64 || args[0].IsBool())
                return MakeF64(static_cast<double>(GetI64(args[0])));
            if (args[0].IsObjRef()) {
                std::string s = ResolveObjRefStringOnly(args[0]);
                return MakeF64(std::atof(s.c_str()));
            }
        }
        return MakeF64(0.0);
    }
    case Efun::Abs: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) {
            return MakeI64(GetI64(args[0]) < 0 ? -GetI64(args[0]) : GetI64(args[0]));
        }
        return MakeI64(0);
    }
    case Efun::Strlen: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            return MakeI64(static_cast<std::int64_t>(s.size()));
        }
        return MakeI64(0);
    }
    case Efun::MapDelete: {
        if (args.size() >= 2) {
            auto *map = GetMappingObjRef(args[0], mappings_);
            if (map) {
                map->Erase(args[1]);
            }
        }
        return Value::Nil();
    }
    case Efun::Capitalize: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            if (!s.empty()) s[0] = static_cast<char>(::toupper(static_cast<unsigned char>(s[0])));
            return InternString(s);
        }
        return Value::Nil();
    }
    case Efun::LowerCase: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return InternString(s);
        }
        return Value::Nil();
    }
    case Efun::UpperCase: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            std::string upper;
            upper.resize(s.size());
            std::transform(s.begin(), s.end(), upper.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return InternString(upper);
        }
        return Value::Nil();
    }
    case Efun::Allocate: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64 && GetI64(args[0]) > 0) {
            std::size_t n = static_cast<std::size_t>(GetI64(args[0]));
            std::vector<Value> arr(n, Value::Nil());
            return AllocateArrayHandle(std::move(arr));
        }
        return Value::Nil();
    }
    case Efun::Reverse: {
        if (!args.empty() && args[0].IsObjRef()) {
            auto *arr = GetArrayObjRef(args[0], arrays_);
            if (arr) {
                std::reverse(arr->begin(), arr->end());
                return args[0];
            }
        }
        return Value::Nil();
    }
    case Efun::Min: {
        if (args.size() >= 2 && args[0].Tag() == ValueTag::Int64 && args[1].Tag() == ValueTag::Int64) {
            return MakeI64(std::min(GetI64(args[0]), GetI64(args[1])));
        }
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) return args[0];
        return MakeI64(0);
    }
    case Efun::Max: {
        if (args.size() >= 2 && args[0].Tag() == ValueTag::Int64 && args[1].Tag() == ValueTag::Int64) {
            return MakeI64(std::max(GetI64(args[0]), GetI64(args[1])));
        }
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) return args[0];
        return MakeI64(0);
    }
    case Efun::Sqrt: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) {
            return MakeI64(static_cast<std::int64_t>(std::sqrt(static_cast<double>(GetI64(args[0])))));
        }
        if (!args.empty() && args[0].IsFloat64()) {
            return MakeI64(static_cast<std::int64_t>(std::sqrt(args[0].AsF64())));
        }
        return MakeI64(0);
    }
    case Efun::Ctime: {
        std::time_t t = 0;
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) t = static_cast<std::time_t>(GetI64(args[0]));
        return InternString(FormatCtimeSafe(t));
    }
    case Efun::Strsrch: {
        if (args.size() >= 2 && args[0].IsObjRef() && args[1].IsObjRef()) {
            std::string haystack = ResolveObjRefStringOnly(args[0]);
            std::string needle = ResolveObjRefStringOnly(args[1]);
            std::size_t pos = haystack.find(needle);
            if (pos == std::string::npos) return MakeI64(-1);
            return MakeI64(static_cast<std::int64_t>(pos));
        }
        return MakeI64(-1);
    }
    case Efun::ReplaceString: {
        if (args.size() >= 3 && args[0].IsObjRef() && args[1].IsObjRef() && args[2].IsObjRef()) {
            std::string str = ResolveObjRefStringOnly(args[0]);
            std::string from = ResolveObjRefStringOnly(args[1]);
            std::string to = ResolveObjRefStringOnly(args[2]);
            std::size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.size(), to);
                pos += to.size();
            }
            return InternString(str);
        }
        return Value::Nil();
    }
    case Efun::SortArray: {
        if (!args.empty() && args[0].IsObjRef()) {
            auto *arr = GetArrayObjRef(args[0], arrays_);
            if (arr) {
                std::sort(arr->begin(), arr->end(), [this](const Value &a, const Value &b) {
                    const bool a_num = (a.Tag() == ValueTag::Int64) || a.IsFloat64();
                    const bool b_num = (b.Tag() == ValueTag::Int64) || b.IsFloat64();
                    if (!a_num || !b_num) return false;
                    const double da = (a.Tag() == ValueTag::Int64) ? static_cast<double>(GetI64(a)) : a.AsF64();
                    const double db = (b.Tag() == ValueTag::Int64) ? static_cast<double>(GetI64(b)) : b.AsF64();
                    return da < db;
                });
                return args[0];
            }
        }
        return Value::Nil();
    }
    case Efun::Instanceof: {
        if (args.size() < 2) return MakeI64(0);
        if (!IsClassObjRef(args[0])) return MakeI64(0);
        std::uint16_t cur = 0;
        if (!ResolveClassTemplateIndex(args[0], &cur)) return MakeI64(0);
        std::string target_name;
        if (args[1].IsObjRef() && IsStringObjRefFull(args[1])) {
            target_name = ResolveObjRefStringOnly(args[1]);
        } else if (args[1].Tag() == ValueTag::Int64) {
            std::uint16_t idx = static_cast<std::uint16_t>(GetI64(args[1]));
            if (idx < BoundChunk().classes.size()) target_name = BoundChunk().classes[idx].name;
        }
        if (target_name.empty()) return MakeI64(0);
        while (cur != kInvalidIndex16 && cur < BoundChunk().classes.size()) {
            if (BoundChunk().classes[cur].name == target_name) return MakeI64(1);
            cur = BoundChunk().classes[cur].parent_class_idx;
        }
        return MakeI64(0);
    }
    }

    return Value::Nil();
}
