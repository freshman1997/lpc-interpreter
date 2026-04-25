#include "nextvm/bytecode/verifier.h"

#include <cstdint>
#include <vector>

#include "nextvm/bytecode/opcode.h"

namespace {

static std::uint16_t ReadU16(const std::vector<std::uint8_t> &code, std::uint32_t *ip) {
    const std::uint32_t i = *ip;
    std::uint16_t v = static_cast<std::uint16_t>(code[i + 1] << 8) | static_cast<std::uint16_t>(code[i]);
    *ip += 2;
    return v;
}

static std::int16_t ReadI16(const std::vector<std::uint8_t> &code, std::uint32_t *ip) {
    return static_cast<std::int16_t>(ReadU16(code, ip));
}

} // namespace

namespace lpc {
namespace nextvm {

static RuntimeError VerifyFunction(const Chunk &chunk, std::uint32_t fid) {
    const FunctionProto &f = chunk.functions[fid];
    if (f.code_start > f.code_end || f.code_end > chunk.code.size()) {
        RuntimeError e;
        e.code = RuntimeErrorCode::InvalidOperand;
        e.message = "function code range invalid";
        e.function = f.name;
        return e;
    }

    std::uint32_t ip = f.code_start;
    int depth = 0;
    bool has_return = false;

    while (ip < f.code_end) {
        const std::uint32_t op_pc = ip;
        Op op = static_cast<Op>(chunk.code[ip++]);

        if (op == Op::LoadIConst) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadIConst operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t idx = ReadU16(chunk.code, &ip);
            if (idx >= chunk.iconst.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "iconst index out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ++depth;
            continue;
        }

        if (op == Op::NewClass || op == Op::SetClassField || op == Op::LoadClassField) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated class operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t idx = ReadU16(chunk.code, &ip);
            if (op == Op::NewClass) {
                if (idx >= chunk.class_field_counts.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "class index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                ++depth;
            } else {
                if (depth < (op == Op::SetClassField ? 2 : 1)) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "class field op stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                if (op == Op::SetClassField) {
                    depth -= 2;
                }
            }
            continue;
        }

        if (op == Op::NewArray) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated NewArray operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t n = ReadU16(chunk.code, &ip);
            if (depth < static_cast<int>(n)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "NewArray stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth = depth - static_cast<int>(n) + 1;
            continue;
        }

        if (op == Op::Index) {
            if (depth < 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Index stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            --depth;
            continue;
        }

        if (op == Op::Pop) {
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Pop stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            --depth;
            continue;
        }

        if (op == Op::Dup) {
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Dup stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ++depth;
            continue;
        }

        if (op == Op::LoadLocal) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadLocal operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t idx = ReadU16(chunk.code, &ip);
            if (idx >= f.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ++depth;
            continue;
        }

        if (op == Op::StoreLocal) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated StoreLocal operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t idx = ReadU16(chunk.code, &ip);
            if (idx >= f.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreLocal stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            --depth;
            continue;
        }

        if (op == Op::Jump) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Jump operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::int16_t rel = ReadI16(chunk.code, &ip);
            std::int64_t target = static_cast<std::int64_t>(ip) + rel;
            if (target < static_cast<std::int64_t>(f.code_start) ||
                target >= static_cast<std::int64_t>(f.code_end)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "Jump target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (op == Op::JumpIfFalse) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated JumpIfFalse operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "JumpIfFalse stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            --depth;
            std::int16_t rel = ReadI16(chunk.code, &ip);
            std::int64_t target = static_cast<std::int64_t>(ip) + rel;
            if (target < static_cast<std::int64_t>(f.code_start) ||
                target >= static_cast<std::int64_t>(f.code_end)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "JumpIfFalse target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (op == Op::CallValue) {
            if (ip + 4 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallValue operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t callee_id = ReadU16(chunk.code, &ip);
            std::uint16_t argc = ReadU16(chunk.code, &ip);
            if (callee_id >= chunk.functions.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "CallValue target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (argc != chunk.functions[callee_id].arity) {
                RuntimeError e;
                e.code = RuntimeErrorCode::ArityMismatch;
                e.message = "CallValue argc mismatch";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (depth < static_cast<int>(argc)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "CallValue stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth = depth - static_cast<int>(argc) + 1;
            continue;
        }

        if (op == Op::Sub || op == Op::Eq || op == Op::Neq || op == Op::Gt ||
            op == Op::Gte || op == Op::Lt || op == Op::Lte || op == Op::LogicAnd || op == Op::LogicOr ||
            op == Op::Mul || op == Op::Div || op == Op::Mod) {
            if (depth < 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "binary op stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            --depth;
            continue;
        }

        if (op == Op::LogicNot) {
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "logic not stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (op == Op::Add) {
            if (depth < 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Add stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            --depth;
            continue;
        }

        if (op == Op::Return) {
            has_return = true;
            break;
        }

        RuntimeError e;
        e.code = RuntimeErrorCode::InvalidOpcode;
        e.message = "unknown NextVM opcode";
        e.function = f.name;
        e.pc = static_cast<int>(op_pc);
        return e;
    }

    if (!has_return) {
        RuntimeError e;
        e.code = RuntimeErrorCode::InvalidOperand;
        e.message = "function has no Return";
        e.function = f.name;
        return e;
    }

    return {};
}

RuntimeError VerifyChunk(const Chunk &chunk) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(chunk.functions.size()); ++i) {
        RuntimeError e = VerifyFunction(chunk, i);
        if (!e.ok()) {
            return e;
        }
    }
    return {};
}

} // namespace nextvm
} // namespace lpc
