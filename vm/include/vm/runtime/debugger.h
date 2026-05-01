#ifndef LPC_VM_RUNTIME_DEBUGGER_H
#define LPC_VM_RUNTIME_DEBUGGER_H

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "vm/runtime/frame.h"
#include "vm/bytecode/chunk.h"
#include "vm/value/value.h"

namespace lpc {
namespace vm {

class Vm;

enum class StepMode {
    None,
    StepInto,
    StepOver,
    StepOut,
    Continue,
};

struct Breakpoint {
    int id = 0;
    std::uint32_t pc = 0;
    std::string func_name;
    int line = -1;
    bool verified = true;
    std::string condition;
};

struct DebugFrame {
    int id = 0;
    std::string name;
    int line = 0;
    std::string source_path;
    int source_line = 0;
    std::string module_name;
    std::uint64_t module_version_id = 0;
    std::uint32_t pc = 0;
};

struct DebugVariable {
    std::string name;
    std::string value;
    std::string type;
    int variables_reference = 0;
};

struct SourceLocation {
    std::string path;
    int line = -1;
};

class Debugger {
public:
    Debugger() : active_(false), step_mode_(StepMode::None), step_depth_(0), step_start_line_(-1), step_has_start_(false), next_bp_id_(1), break_on_exceptions_(false), last_break_exception_(false) {}

    void set_active(bool a) { active_ = a; }
    bool active() const { return active_; }

    void set_break_on_exceptions(bool v) { break_on_exceptions_ = v; }
    bool break_on_exceptions() const { return break_on_exceptions_; }

    void set_last_break_exception(bool v) { last_break_exception_ = v; }
    bool last_break_exception() const { return last_break_exception_; }

    int AddBreakpoint(std::uint32_t pc, const std::string &func_name = "", int line = -1) {
        int id = next_bp_id_++;
        Breakpoint bp;
        bp.id = id;
        bp.pc = pc;
        bp.func_name = func_name;
        bp.line = line;
        breakpoints_.push_back(bp);
        return id;
    }

    int AddBreakpointByLine(const Chunk &chunk, int line) {
        for (const auto &le : chunk.line_table) {
            if (static_cast<int>(le.line) == line) {
                std::string fn;
                for (const auto &f : chunk.functions) {
                    if (le.pc >= f.code_start && le.pc < f.code_end) {
                        fn = f.name;
                        break;
                    }
                }
                return AddBreakpoint(le.pc, fn, line);
            }
        }
        int nearest_line = -1;
        std::uint32_t nearest_pc = 0;
        int best_dist = 0x7fffffff;
        for (const auto &le : chunk.line_table) {
            int dist = static_cast<int>(le.line) - line;
            if (dist > 0 && dist < best_dist) {
                best_dist = dist;
                nearest_line = static_cast<int>(le.line);
                nearest_pc = le.pc;
            }
        }
        if (nearest_line > 0) {
            std::string fn;
            for (const auto &f : chunk.functions) {
                if (nearest_pc >= f.code_start && nearest_pc < f.code_end) {
                    fn = f.name;
                    break;
                }
            }
            int id = AddBreakpoint(nearest_pc, fn, nearest_line);
            for (auto &bp : breakpoints_) {
                if (bp.id == id) {
                    bp.line = nearest_line;
                    break;
                }
            }
            return id;
        }
        int id = next_bp_id_++;
        Breakpoint bp;
        bp.id = id;
        bp.line = line;
        bp.verified = false;
        breakpoints_.push_back(bp);
        return id;
    }

    int AddBreakpointByFuncLine(const Chunk &chunk, const std::string &func_name, int line) {
        for (const auto &f : chunk.functions) {
            if (f.name == func_name) {
                for (const auto &le : chunk.line_table) {
                    if (le.pc >= f.code_start && le.pc < f.code_end &&
                        static_cast<int>(le.line) == line) {
                        return AddBreakpoint(le.pc, func_name, line);
                    }
                }
            }
        }
        return AddBreakpointByLine(chunk, line);
    }

    bool RemoveBreakpoint(int id) {
        for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
            if (it->id == id) {
                breakpoints_.erase(it);
                return true;
            }
        }
        return false;
    }

    void ClearBreakpoints() { breakpoints_.clear(); }

    void ClearBreakpointsByLine(int line) {
        breakpoints_.erase(
            std::remove_if(breakpoints_.begin(), breakpoints_.end(),
                [line](const Breakpoint &bp) { return bp.line == line; }),
            breakpoints_.end());
    }

    const std::vector<Breakpoint> &breakpoints() const { return breakpoints_; }
    std::vector<Breakpoint> &breakpoints() { return breakpoints_; }

    void SetBreakpointCondition(int id, const std::string &condition) {
        for (auto &bp : breakpoints_) {
            if (bp.id == id) {
                bp.condition = condition;
                return;
            }
        }
    }

    void SetStepMode(StepMode mode, std::uint32_t depth = 0) {
        step_mode_ = mode;
        step_depth_ = depth;
        step_has_start_ = false;
        step_start_path_.clear();
        step_start_line_ = -1;
    }

    void SetSourceStepMode(StepMode mode, std::uint32_t depth,
                           const Chunk &chunk, std::uint32_t pc) {
        SetStepMode(mode, depth);
        if (mode == StepMode::StepInto || mode == StepMode::StepOver) {
            SourceLocation loc = FindSourceLocation(chunk, pc);
            if (loc.line > 0) {
                step_start_path_ = loc.path;
                step_start_line_ = loc.line;
                step_has_start_ = true;
            }
        }
    }

    StepMode step_mode() const { return step_mode_; }
    std::uint32_t step_depth() const { return step_depth_; }

    bool ShouldBreak(std::uint32_t pc, std::uint32_t frame_depth,
                     const Chunk &chunk, const std::vector<Frame> &frames,
                     const std::vector<Value> &stack) {
        if (!active_) return false;

        for (const auto &bp : breakpoints_) {
            if (bp.pc == pc && bp.verified) {
                if (!bp.condition.empty()) {
                    if (!EvaluateCondition(bp.condition, chunk, frames, stack)) {
                        continue;
                    }
                }
                return true;
            }
        }

        switch (step_mode_) {
        case StepMode::StepInto:
            return HasSourceLocationChanged(chunk, pc);
        case StepMode::StepOver:
            return frame_depth <= step_depth_ && HasSourceLocationChanged(chunk, pc);
        case StepMode::StepOut:
            return frame_depth < step_depth_;
        case StepMode::None:
        case StepMode::Continue:
            break;
        }
        return false;
    }

    void ClearStepOnBreak(std::uint32_t frame_depth) {
        if (step_mode_ == StepMode::StepInto) {
            step_mode_ = StepMode::None;
            step_has_start_ = false;
        } else if (step_mode_ == StepMode::StepOver && frame_depth <= step_depth_) {
            step_mode_ = StepMode::None;
            step_has_start_ = false;
        } else if (step_mode_ == StepMode::StepOut && frame_depth < step_depth_) {
            step_mode_ = StepMode::None;
            step_has_start_ = false;
        }
    }

    int FindLine(const Chunk &chunk, std::uint32_t pc) const {
        int best = -1;
        for (const auto &le : chunk.line_table) {
            if (le.pc <= pc) {
                best = static_cast<int>(le.line);
            } else {
                break;
            }
        }
        return best;
    }

    std::string FindFunction(const Chunk &chunk, std::uint32_t pc) const {
        for (const auto &f : chunk.functions) {
            if (pc >= f.code_start && pc < f.code_end) {
                return f.name;
            }
        }
        return "";
    }

    std::string SourcePath(const Chunk &chunk) const {
        if (!chunk.debug_info.source_file.empty()) {
            return chunk.debug_info.source_file;
        }
        return chunk.module_name;
    }

    SourceLocation FindSourceLocation(const Chunk &chunk, std::uint32_t pc) const {
        SourceLocation loc;
        loc.path = SourcePath(chunk);
        loc.line = FindLine(chunk, pc);
        for (const auto &sme : chunk.debug_info.source_map) {
            if (sme.output_line == loc.line) {
                loc.path = sme.source_path;
                loc.line = sme.source_line;
                break;
            }
        }
        return loc;
    }

    void PrintLocation(const Chunk &chunk, std::uint32_t pc) const {
        int line = FindLine(chunk, pc);
        if (line < 0) {
            std::cerr << "  (unknown line)" << std::endl;
            return;
        }
        std::string src_file = chunk.debug_info.source_file;
        int src_line = line;
        for (const auto &sme : chunk.debug_info.source_map) {
            if (sme.output_line == line) {
                src_file = sme.source_path;
                src_line = sme.source_line;
                break;
            }
        }
        if (src_file.empty()) {
            std::cerr << "  (no source file info)" << std::endl;
            return;
        }
        auto src = SourceLines(src_file);
        if (src.empty()) {
            std::cerr << "  " << src_file << ":" << src_line
                      << " (source not found)" << std::endl;
            return;
        }
        int context = 3;
        int start = (src_line - context > 0) ? src_line - context : 1;
        int end = (src_line + context <= static_cast<int>(src.size()))
            ? src_line + context : static_cast<int>(src.size());
        for (int i = start; i <= end; ++i) {
            const char *marker = (i == src_line) ? " =>" : "   ";
            const std::string &text = src[static_cast<std::size_t>(i - 1)];
            std::cerr << marker << " " << i << "  " << text << std::endl;
        }
    }

    void PrintBacktrace(const Chunk &chunk, const std::vector<Frame> &frames) const {
        auto resolver = [](std::uint64_t) -> const Chunk * { return nullptr; };
        PrintBacktrace(chunk, frames, resolver);
    }

    template <typename ChunkResolver>
    void PrintBacktrace(const Chunk &chunk, const std::vector<Frame> &frames, ChunkResolver resolver) const {
        for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i) {
            const Frame &fr = frames[i];
            const Chunk *frame_chunk = &chunk;
            if (fr.module_version_id != 0) {
                const Chunk *resolved = resolver(fr.module_version_id);
                if (resolved) frame_chunk = resolved;
            }
            std::string fn = "?";
            if (fr.func_id < frame_chunk->functions.size()) {
                fn = frame_chunk->functions[fr.func_id].name;
            }
            int line = FindLine(*frame_chunk, fr.ip);
            std::string src_file = frame_chunk->debug_info.source_file;
            int src_line = line;
            for (const auto &sme : frame_chunk->debug_info.source_map) {
                if (sme.output_line == line) {
                    src_file = sme.source_path;
                    src_line = sme.source_line;
                    break;
                }
            }
            std::cerr << "  #" << (frames.size() - 1 - i) << " " << fn
                      << " at " << src_file << ":" << src_line
                      << " (pc=" << fr.ip << ", version=" << fr.module_version_id << ")" << std::endl;
        }
    }

    std::vector<DebugFrame> GetBacktrace(const Chunk &chunk, const std::vector<Frame> &frames) const {
        auto resolver = [](std::uint64_t) -> const Chunk * { return nullptr; };
        return GetBacktrace(chunk, frames, resolver);
    }

    template <typename ChunkResolver>
    std::vector<DebugFrame> GetBacktrace(const Chunk &chunk, const std::vector<Frame> &frames, ChunkResolver resolver) const {
        std::vector<DebugFrame> result;
        for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i) {
            const Frame &fr = frames[i];
            const Chunk *frame_chunk = &chunk;
            if (fr.module_version_id != 0) {
                const Chunk *resolved = resolver(fr.module_version_id);
                if (resolved) frame_chunk = resolved;
            }
            DebugFrame df;
            df.id = static_cast<int>(frames.size() - 1 - i);
            if (fr.func_id < frame_chunk->functions.size()) {
                df.name = frame_chunk->functions[fr.func_id].name;
            } else {
                df.name = "?";
            }
            df.line = FindLine(*frame_chunk, fr.ip);
            df.source_path = SourcePath(*frame_chunk);
            df.source_line = df.line;
            for (const auto &sme : frame_chunk->debug_info.source_map) {
                if (sme.output_line == df.line) {
                    df.source_path = sme.source_path;
                    df.source_line = sme.source_line;
                    break;
                }
            }
            df.module_name = fr.module_name;
            df.module_version_id = fr.module_version_id;
            df.pc = fr.ip;
            result.push_back(df);
        }
        return result;
    }

    void PrintLocals(const Chunk &chunk, const Frame &frame,
                     const std::vector<Value> &stack) const {
        if (frame.func_id >= chunk.functions.size()) return;
        const auto &fproto = chunk.functions[frame.func_id];
        const FunctionDebugInfo *fdi = nullptr;
        if (frame.func_id < chunk.debug_info.function_debug.size()) {
            fdi = &chunk.debug_info.function_debug[frame.func_id];
        }

        std::cerr << "  args (" << static_cast<int>(fproto.arity) << "):" << std::endl;
        for (std::uint16_t i = 0; i < fproto.arity; ++i) {
            std::string name = fdi && i < fdi->param_names.size()
                ? fdi->param_names[i] : ("arg" + std::to_string(i));
            std::uint32_t slot = frame.base + i;
            if (slot < stack.size()) {
                std::cerr << "    " << name << " = " << FormatValue(stack[slot]) << std::endl;
            }
        }

        int n_local_names = fdi
            ? static_cast<int>(fdi->local_names.size()) : 0;
        std::cerr << "  locals (" << n_local_names << "):" << std::endl;
        for (int i = 0; i < n_local_names; ++i) {
            std::uint32_t slot = frame.base + fproto.arity + i;
            if (slot < stack.size()) {
                std::cerr << "    " << fdi->local_names[i]
                          << " = " << FormatValue(stack[slot]) << std::endl;
            }
        }

        if (fdi) {
            std::cerr << "  upvalues (" << fdi->upvalue_names.size() << "):" << std::endl;
            for (std::size_t i = 0; i < fdi->upvalue_names.size(); ++i) {
                std::cerr << "    " << fdi->upvalue_names[i] << std::endl;
            }
        }
    }

    std::vector<DebugVariable> GetArgs(const Chunk &chunk, const Frame &frame,
                                       const std::vector<Value> &stack) const {
        std::vector<DebugVariable> result;
        if (frame.func_id >= chunk.functions.size()) return result;
        const auto &fproto = chunk.functions[frame.func_id];
        const FunctionDebugInfo *fdi = nullptr;
        if (frame.func_id < chunk.debug_info.function_debug.size()) {
            fdi = &chunk.debug_info.function_debug[frame.func_id];
        }
        for (std::uint16_t i = 0; i < fproto.arity; ++i) {
            DebugVariable dv;
            dv.name = fdi && i < fdi->param_names.size()
                ? fdi->param_names[i] : ("arg" + std::to_string(i));
            std::uint32_t slot = frame.base + i;
            if (slot < stack.size()) {
                dv.value = FormatValue(stack[slot]);
                dv.type = TypeName(stack[slot]);
            }
            result.push_back(dv);
        }
        return result;
    }

    std::vector<DebugVariable> GetLocals(const Chunk &chunk, const Frame &frame,
                                         const std::vector<Value> &stack) const {
        std::vector<DebugVariable> result;
        if (frame.func_id >= chunk.functions.size()) return result;
        const auto &fproto = chunk.functions[frame.func_id];
        const FunctionDebugInfo *fdi = nullptr;
        if (frame.func_id < chunk.debug_info.function_debug.size()) {
            fdi = &chunk.debug_info.function_debug[frame.func_id];
        }
        int n_local_names = fdi
            ? static_cast<int>(fdi->local_names.size()) : 0;
        for (int i = 0; i < n_local_names; ++i) {
            DebugVariable dv;
            dv.name = fdi->local_names[i];
            std::uint32_t slot = frame.base + fproto.arity + i;
            if (slot < stack.size()) {
                dv.value = FormatValue(stack[slot]);
                dv.type = TypeName(stack[slot]);
            }
            result.push_back(dv);
        }
        return result;
    }

    std::vector<DebugVariable> GetGlobals(const Chunk &chunk) const {
        std::vector<DebugVariable> result;
        for (std::size_t i = 0; i < chunk.globals.size(); ++i) {
            DebugVariable dv;
            dv.name = i < chunk.global_names.size()
                ? chunk.global_names[i] : ("g" + std::to_string(i));
            dv.value = FormatValue(chunk.globals[i]);
            dv.type = TypeName(chunk.globals[i]);
            result.push_back(dv);
        }
        return result;
    }

    static std::vector<std::string> SourceLines(const std::string &path) {
        std::vector<std::string> lines;
        std::ifstream ifs(path);
        if (!ifs.is_open()) return lines;
        std::string l;
        while (std::getline(ifs, l)) {
            lines.push_back(l);
        }
        return lines;
    }

    void PrintSource(const Chunk &chunk, std::uint32_t pc, int context = 3) const {
        if (chunk.debug_info.source_file.empty()) {
            std::cerr << "  (no source file info)" << std::endl;
            return;
        }
        int line = FindLine(chunk, pc);
        if (line < 0) {
            std::cerr << "  (unknown line)" << std::endl;
            return;
        }
        auto src = SourceLines(chunk.debug_info.source_file);
        if (src.empty()) {
            std::cerr << "  " << chunk.debug_info.source_file << ":" << line
                      << " (source not found)" << std::endl;
            return;
        }
        int start = (line - context > 0) ? line - context : 1;
        int end = (line + context <= static_cast<int>(src.size()))
            ? line + context : static_cast<int>(src.size());
        for (int i = start; i <= end; ++i) {
            const char *marker = (i == line) ? " =>" : "   ";
            const std::string &text = src[static_cast<std::size_t>(i - 1)];
            std::cerr << marker << " " << i << "  " << text << std::endl;
        }
    }

    void PrintBreakpoints() const {
        if (breakpoints_.empty()) {
            std::cerr << "  No breakpoints." << std::endl;
            return;
        }
        for (const auto &bp : breakpoints_) {
            std::cerr << "  #" << bp.id << " pc=" << bp.pc;
            if (!bp.func_name.empty()) {
                std::cerr << " func=" << bp.func_name;
            }
            if (bp.line >= 0) {
                std::cerr << " line=" << bp.line;
            }
            if (!bp.verified) {
                std::cerr << " (unverified)";
            }
            if (!bp.condition.empty()) {
                std::cerr << " if " << bp.condition;
            }
            std::cerr << std::endl;
        }
    }

    static std::string FormatValue(const Value &v) {
        switch (v.Tag()) {
        case ValueTag::Nil:
            return "nil";
        case ValueTag::Bool:
            return v.AsI64() ? "true" : "false";
        case ValueTag::Int64:
            return std::to_string(v.AsI64());
        case ValueTag::Float64: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", v.AsF64());
            return buf;
        }
        case ValueTag::ObjRef: {
            std::uintptr_t raw = v.AsObj();
            if (raw > 0 && raw < 0x080000000ULL) {
                return "<string>";
            }
            if (raw >= 0x080000000ULL && raw < 0x100000000ULL) {
                return "<func:" + std::to_string(raw - 0x080000000ULL) + ">";
            }
            if (raw >= 0x100000000ULL && raw < 0x200000000ULL) {
                return "<array:" + std::to_string(raw - 0x100000000ULL) + ">";
            }
            if (raw >= 0x200000000ULL && raw < 0x300000000ULL) {
                return "<mapping:" + std::to_string(raw - 0x200000000ULL) + ">";
            }
            if (raw >= 0x300000000ULL && raw < 0x400000000ULL) {
                return "<class:" + std::to_string(raw - 0x300000000ULL) + ">";
            }
            if (raw >= 0x400000000ULL) {
                return "<object:" + std::to_string(raw - 0x400000000ULL) + ">";
            }
            return "<obj>";
        }
        case ValueTag::Closure:
            return "<closure>";
        default:
            return "<?>";
        }
    }

    static std::string FormatValueEx(const Value &v, const Vm &vm);

    static std::string TypeName(const Value &v) {
        switch (v.Tag()) {
        case ValueTag::Nil: return "nil";
        case ValueTag::Bool: return "bool";
        case ValueTag::Int64: return "int";
        case ValueTag::Float64: return "float";
        case ValueTag::ObjRef: return "object";
        case ValueTag::Closure: return "function";
        default: return "unknown";
        }
    }

    Value ResolveVariable(const std::string &name,
                          const Chunk &chunk,
                          const std::vector<Frame> &frames,
                          const std::vector<Value> &stack) const {
        Value out;
        if (TryResolveVariable(name, chunk, frames, stack, &out)) {
            return out;
        }
        return Value::Nil();
    }

    bool TryResolveVariable(const std::string &name,
                            const Chunk &chunk,
                            const std::vector<Frame> &frames,
                            const std::vector<Value> &stack,
                            Value *out) const {
        if (!frames.empty()) {
            const Frame &fr = frames.back();
            if (fr.func_id < chunk.functions.size()) {
                const auto &fproto = chunk.functions[fr.func_id];
                const FunctionDebugInfo *fdi = nullptr;
                if (fr.func_id < chunk.debug_info.function_debug.size()) {
                    fdi = &chunk.debug_info.function_debug[fr.func_id];
                }
                for (std::uint16_t i = 0; i < fproto.arity; ++i) {
                    std::string pname = fdi && i < fdi->param_names.size()
                        ? fdi->param_names[i] : ("arg" + std::to_string(i));
                    if (pname == name) {
                        std::uint32_t slot = fr.base + i;
                        if (out) *out = slot < stack.size() ? stack[slot] : Value::Nil();
                        return true;
                    }
                }
                if (fdi) {
                    for (int i = 0; i < static_cast<int>(fdi->local_names.size()); ++i) {
                        if (fdi->local_names[i] == name) {
                            std::uint32_t slot = fr.base + fproto.arity + i;
                            if (out) *out = slot < stack.size() ? stack[slot] : Value::Nil();
                            return true;
                        }
                    }
                }
            }
        }
        for (std::size_t i = 0; i < chunk.global_names.size(); ++i) {
            if (chunk.global_names[i] == name) {
                if (out) *out = i < chunk.globals.size() ? chunk.globals[i] : Value::Nil();
                return true;
            }
        }
        return false;
    }

    static Value ResolveExpr(const std::string &expr,
                             const Chunk &chunk,
                             const std::vector<Frame> &frames,
                             const std::vector<Value> &stack,
                             const Vm &vm);

    static bool ValueToBool(const Value &v) {
        if (v.IsBool()) return v.AsI64() != 0;
        if (v.Tag() == ValueTag::Int64) return v.AsI64() != 0;
        if (v.IsFloat64()) return v.AsF64() != 0.0;
        if (v.IsNil()) return false;
        return true;
    }

    static Value ParseLiteral(const std::string &s) {
        if (s == "nil") return Value::Nil();
        if (s == "true") { return Value::FromBool(true); }
        if (s == "false") { return Value::FromBool(false); }
        try {
            std::size_t pos = 0;
            std::int64_t iv = std::stoll(s, &pos, 0);
            if (pos == s.size()) {
                return Value::FromI64(iv);

            }
        } catch (...) {}
        try {
            std::size_t pos = 0;
            double dv = std::stod(s, &pos);
            if (pos == s.size()) {
                return Value::FromF64(dv);
            }
        } catch (...) {}
        return Value::Nil();
    }

    static double ValueToDouble(const Value &v) {
        if (v.Tag() == ValueTag::Int64) return static_cast<double>(v.AsI64());
        if (v.IsFloat64()) return v.AsF64();
        if (v.IsBool()) return static_cast<double>(v.AsI64());
        return 0.0;
    }

    static bool CompareValues(const Value &lhs, const Value &rhs, const std::string &op) {
        if (op == "==" || op == "=") {
            if (lhs.Tag() != rhs.Tag()) {
                if ((lhs.Tag() == ValueTag::Int64 || lhs.IsFloat64()) &&
                    (rhs.Tag() == ValueTag::Int64 || rhs.IsFloat64())) {
                    return ValueToDouble(lhs) == ValueToDouble(rhs);
                }
                return false;
            }
            switch (lhs.Tag()) {
            case ValueTag::Nil: return true;
            case ValueTag::Bool: return lhs.AsI64() == rhs.AsI64();
            case ValueTag::Int64: return lhs.AsI64() == rhs.AsI64();
            case ValueTag::Float64: return lhs.AsF64() == rhs.AsF64();
            default: return lhs.AsObj() == rhs.AsObj();
            }
        }
        if (op == "!=" || op == "/=") {
            return !CompareValues(lhs, rhs, "==");
        }
        if ((lhs.Tag() == ValueTag::Int64 || lhs.IsFloat64()) &&
            (rhs.Tag() == ValueTag::Int64 || rhs.IsFloat64())) {
            double l = ValueToDouble(lhs);
            double r = ValueToDouble(rhs);
            if (op == "<") return l < r;
            if (op == "<=") return l <= r;
            if (op == ">") return l > r;
            if (op == ">=") return l >= r;
        }
        return false;
    }

    bool EvaluateCondition(const std::string &condition,
                           const Chunk &chunk,
                           const std::vector<Frame> &frames,
                           const std::vector<Value> &stack) const {
        std::string cond = condition;
        std::size_t ws = cond.find_first_not_of(" \t");
        if (ws != std::string::npos && ws > 0) cond = cond.substr(ws);

        auto or_pos = cond.find("||");
        if (or_pos != std::string::npos) {
            std::string left = cond.substr(0, or_pos);
            std::string right = cond.substr(or_pos + 2);
            return EvaluateCondition(left, chunk, frames, stack)
                || EvaluateCondition(right, chunk, frames, stack);
        }

        auto and_pos = cond.find("&&");
        if (and_pos != std::string::npos) {
            std::string left = cond.substr(0, and_pos);
            std::string right = cond.substr(and_pos + 2);
            return EvaluateCondition(left, chunk, frames, stack)
                && EvaluateCondition(right, chunk, frames, stack);
        }

        std::string ops[] = {"!=", "<=", ">=", "==", "<", ">", "="};
        for (const auto &op : ops) {
            auto pos = cond.find(op);
            if (pos != std::string::npos) {
                std::string lhs_name = cond.substr(0, pos);
                std::string rhs_str = cond.substr(pos + op.size());
                while (!lhs_name.empty() && (lhs_name.back() == ' ' || lhs_name.back() == '\t'))
                    lhs_name.pop_back();
                while (!rhs_str.empty() && (rhs_str.front() == ' ' || rhs_str.front() == '\t'))
                    rhs_str.erase(rhs_str.begin());

                Value lhs = ResolveVariable(lhs_name, chunk, frames, stack);
                Value rhs = ParseLiteral(rhs_str);
                if (lhs.IsNil() && rhs.IsNil()) {
                    lhs = ParseLiteral(lhs_name);
                    rhs = ResolveVariable(rhs_str, chunk, frames, stack);
                }
                if (lhs.IsNil() && rhs.IsNil()) {
                    lhs = ParseLiteral(lhs_name);
                    rhs = ParseLiteral(rhs_str);
                }
                return CompareValues(lhs, rhs, op);
            }
        }

        Value v = ResolveVariable(cond, chunk, frames, stack);
        return ValueToBool(v);
    }

private:
    bool HasSourceLocationChanged(const Chunk &chunk, std::uint32_t pc) const {
        if (!step_has_start_) return true;
        SourceLocation loc = FindSourceLocation(chunk, pc);
        if (loc.line <= 0) return false;
        return loc.line != step_start_line_ || loc.path != step_start_path_;
    }

    bool active_;
    StepMode step_mode_;
    std::uint32_t step_depth_;
    std::string step_start_path_;
    int step_start_line_;
    bool step_has_start_;
    int next_bp_id_;
    bool break_on_exceptions_;
    bool last_break_exception_;
    std::vector<Breakpoint> breakpoints_;
};

} // namespace vm
} // namespace lpc

#endif
