#include "cli/debug_repl.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vm/runtime/vm.h"
#include "vm/runtime/debugger.h"

namespace lpc {
namespace vm {

static std::string Trim(const std::string &s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> Split(const std::string &s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (std::getline(iss, tok, delim)) {
        std::string t = Trim(tok);
        if (!t.empty()) tokens.push_back(t);
    }
    return tokens;
}

RuntimeError RunDebugReplStep(Vm &vm, std::uint32_t pc) {
    Debugger &dbg = vm.debugger();
    const Chunk &chunk = vm.chunk();
    const std::vector<Frame> &frames = vm.frames();
    const Value *stack = vm.StackData();
    const std::size_t stack_size = vm.StackSize();
    auto chunk_resolver = [&vm](std::uint64_t version_id) -> const Chunk * {
        return vm.GetChunkForVersion(version_id);
    };

    dbg.PrintLocation(chunk, pc);

    while (true) {
        std::cerr << "(lpcdbg) " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            return RuntimeError::Error(RuntimeErrorCode::InternalError, "debugger input closed");
        }
        line = Trim(line);
        if (line.empty()) continue;

        std::vector<std::string> parts = Split(line, ' ');
        const std::string &cmd = parts[0];

        if (cmd == "h" || cmd == "help") {
            std::cerr << "Commands:" << std::endl;
            std::cerr << "  b(reak) [line|func:line] [if cond] - set breakpoint" << std::endl;
            std::cerr << "  info b(reak)                - list breakpoints" << std::endl;
            std::cerr << "  del(ete) <id>               - delete breakpoint" << std::endl;
            std::cerr << "  clear                       - clear all breakpoints" << std::endl;
            std::cerr << "  c(ontinue)                  - continue execution" << std::endl;
            std::cerr << "  n(ext)                      - step over" << std::endl;
            std::cerr << "  s(tep)                      - step into" << std::endl;
            std::cerr << "  fin(ish)                    - step out" << std::endl;
            std::cerr << "  bt / backtrace              - show backtrace" << std::endl;
            std::cerr << "  l(ist)                      - show source location" << std::endl;
            std::cerr << "  locals                      - show local variables" << std::endl;
            std::cerr << "  args                        - show arguments" << std::endl;
            std::cerr << "  globals                     - show global variables" << std::endl;
            std::cerr << "  p(rint) <expr>              - print value (var, arr[0], obj.field)" << std::endl;
            std::cerr << "  q(uit)                      - quit debugger" << std::endl;
            continue;
        }

        if (cmd == "b" || cmd == "break") {
            if (parts.size() < 2) {
                std::cerr << "  Usage: b <line> | b <func:line> [if <condition>]" << std::endl;
                continue;
            }
            std::string spec = parts[1];

            std::string condition;
            for (std::size_t pi = 2; pi < parts.size(); ++pi) {
                if (parts[pi] == "if" && pi + 1 < parts.size()) {
                    for (std::size_t pj = pi + 1; pj < parts.size(); ++pj) {
                        if (!condition.empty()) condition += " ";
                        condition += parts[pj];
                    }
                    break;
                }
            }

            auto colon = spec.find(':');
            int id = -1;
            int ln = 0;
            if (colon != std::string::npos) {
                std::string func = spec.substr(0, colon);
                ln = std::atoi(spec.substr(colon + 1).c_str());
                id = dbg.AddBreakpointByFuncLine(chunk, func, ln);
            } else {
                ln = std::atoi(spec.c_str());
                id = dbg.AddBreakpointByLine(chunk, ln);
            }

            int actual_line = ln;
            bool verified = true;
            for (const auto &bp : dbg.breakpoints()) {
                if (bp.id == id) {
                    verified = bp.verified;
                    if (bp.line >= 0) actual_line = bp.line;
                }
            }

            if (!condition.empty()) {
                dbg.SetBreakpointCondition(id, condition);
            }

            if (actual_line != ln) {
                std::cerr << "  Breakpoint #" << id << " adjusted to line " << actual_line
                          << " (requested " << ln << ")" << std::endl;
            } else {
                std::cerr << "  Breakpoint #" << id << " at line " << ln;
                if (!verified) std::cerr << " (unverified)";
                std::cerr << std::endl;
            }
            if (!condition.empty()) {
                std::cerr << "  Condition: " << condition << std::endl;
            }
            continue;
        }

        if (cmd == "info") {
            if (parts.size() > 1 && (parts[1] == "b" || parts[1] == "break" || parts[1] == "breakpoints")) {
                dbg.PrintBreakpoints();
            } else {
                std::cerr << "  Usage: info b(reak)" << std::endl;
            }
            continue;
        }

        if (cmd == "del" || cmd == "delete") {
            if (parts.size() < 2) {
                std::cerr << "  Usage: del <id>" << std::endl;
                continue;
            }
            int id = std::atoi(parts[1].c_str());
            if (dbg.RemoveBreakpoint(id)) {
                std::cerr << "  Deleted breakpoint #" << id << std::endl;
            } else {
                std::cerr << "  No breakpoint #" << id << std::endl;
            }
            continue;
        }

        if (cmd == "clear") {
            dbg.ClearBreakpoints();
            std::cerr << "  All breakpoints cleared." << std::endl;
            continue;
        }

        if (cmd == "c" || cmd == "continue") {
            dbg.SetStepMode(StepMode::Continue, static_cast<std::uint32_t>(frames.size()));
            break;
        }

        if (cmd == "n" || cmd == "next") {
            dbg.SetStepMode(StepMode::StepOver, static_cast<std::uint32_t>(frames.size()));
            break;
        }

        if (cmd == "s" || cmd == "step") {
            dbg.SetStepMode(StepMode::StepInto, 0);
            break;
        }

        if (cmd == "fin" || cmd == "finish") {
            dbg.SetStepMode(StepMode::StepOut, static_cast<std::uint32_t>(frames.size()));
            break;
        }

        if (cmd == "bt" || cmd == "backtrace") {
            dbg.PrintBacktrace(chunk, frames, chunk_resolver);
            continue;
        }

        if (cmd == "l" || cmd == "list") {
            dbg.PrintLocation(chunk, pc);
            dbg.PrintSource(chunk, pc);
            continue;
        }

        if (cmd == "locals") {
            if (!frames.empty()) {
                dbg.PrintLocals(chunk, frames.back(), stack, stack_size);
            }
            continue;
        }

        if (cmd == "args") {
            if (!frames.empty()) {
                const Frame &fr = frames.back();
                if (fr.func_id < chunk.functions.size()) {
                    const auto &fproto = chunk.functions[fr.func_id];
                    const FunctionDebugInfo *fdi = nullptr;
                    if (fr.func_id < chunk.debug_info.function_debug.size()) {
                        fdi = &chunk.debug_info.function_debug[fr.func_id];
                    }
                    std::cerr << "  args (" << static_cast<int>(fproto.arity) << "):" << std::endl;
                    for (std::uint16_t i = 0; i < fproto.arity; ++i) {
                        std::string name = fdi && i < fdi->param_names.size()
                            ? fdi->param_names[i] : ("arg" + std::to_string(i));
                        std::uint32_t slot = fr.base + i;
                        if (slot < stack_size) {
                            std::cerr << "    " << name << " = " << Debugger::FormatValue(stack[slot]) << std::endl;
                        }
                    }
                }
            }
            continue;
        }

        if (cmd == "globals") {
            auto gvars = dbg.GetGlobals(chunk);
            std::cerr << "  globals (" << gvars.size() << "):" << std::endl;
            for (const auto &gv : gvars) {
                std::cerr << "    " << gv.name << " = " << gv.value << std::endl;
            }
            continue;
        }

        if (cmd == "p" || cmd == "print") {
            if (parts.size() < 2) {
                std::cerr << "  Usage: p <expr>" << std::endl;
                continue;
            }
            std::string expr = line.substr(line.find_first_of(" \t") + 1);
            expr = Trim(expr);

            bool has_dots = expr.find('.') != std::string::npos;
            bool has_brackets = expr.find('[') != std::string::npos;

            if (has_dots || has_brackets) {
                Value result = Debugger::ResolveExpr(expr, chunk, frames, stack, stack_size, vm);
                if (!result.IsNil() || has_dots || has_brackets) {
                    std::cerr << "  " << expr << " = " << Debugger::FormatValueEx(result, vm) << std::endl;
                } else {
                    std::cerr << "  unknown: " << expr << std::endl;
                }
                continue;
            }

            Value v = dbg.ResolveVariable(expr, chunk, frames, stack, stack_size);
            if (!v.IsNil()) {
                std::cerr << "  " << expr << " = " << Debugger::FormatValueEx(v, vm) << std::endl;
                continue;
            }
            v = Debugger::ParseLiteral(expr);
            if (!v.IsNil()) {
                std::cerr << "  " << Debugger::FormatValueEx(v, vm) << std::endl;
                continue;
            }
            std::cerr << "  unknown: " << expr << std::endl;
            continue;
        }

        if (cmd == "q" || cmd == "quit") {
            return RuntimeError::Error(RuntimeErrorCode::InternalError, "debugger quit");
        }

        std::cerr << "  Unknown command: " << cmd << " (type 'help' for commands)" << std::endl;
    }

    return RuntimeError::Ok();
}

} // namespace vm
} // namespace lpc
