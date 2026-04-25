#include "nextvm/runtime/vm.h"

#include "nextvm/bytecode/opcode.h"
#include "nextvm/bytecode/verifier.h"

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

static bool IsTruthy(const lpc::nextvm::Value &v) {
    using lpc::nextvm::ValueTag;
    switch (v.tag) {
    case ValueTag::Nil:
        return false;
    case ValueTag::Bool:
    case ValueTag::Int64:
        return v.as.i64 != 0;
    case ValueTag::Float64:
        return v.as.f64 != 0.0;
    case ValueTag::ObjRef:
        return v.as.obj != nullptr;
    }
    return false;
}

} // namespace

namespace lpc {
namespace nextvm {

RuntimeError Vm::LoadChunk(const Chunk &chunk) {
    chunk_ = chunk;
    value_stack_.clear();
    frames_.clear();
    last_result_ = Value::Nil();
    return {};
}

RuntimeError Vm::RunEntry(const char *function_name) {
    RuntimeError verify = VerifyChunk(chunk_);
    if (!verify.ok()) {
        return verify;
    }

    if (!function_name || !*function_name) {
        RuntimeError e;
        e.code = RuntimeErrorCode::InvalidOperand;
        e.message = "empty entry function";
        return e;
    }

    int func_id = -1;
    for (int i = 0; i < static_cast<int>(chunk_.functions.size()); ++i) {
        if (chunk_.functions[i].name == function_name) {
            func_id = i;
            break;
        }
    }
    if (func_id < 0) {
        RuntimeError e;
        e.code = RuntimeErrorCode::NotCallable;
        e.message = std::string("entry function not found: ") + function_name;
        return e;
    }

    const FunctionProto &f = chunk_.functions[func_id];
    Frame frame;
    frame.func_id = static_cast<std::uint32_t>(func_id);
    frame.ip = f.code_start;
    frame.base = 0;
    frame.stack_top = 0;
    frames_.clear();
    frames_.push_back(frame);
    value_stack_.clear();
    const std::size_t local_slots = static_cast<std::size_t>(f.nlocals);
    for (std::size_t i = 0; i < local_slots; ++i) {
        value_stack_.push_back(Value::Nil());
    }
    frames_.back().stack_top = static_cast<std::uint32_t>(value_stack_.size());

    std::vector<std::vector<Value>> class_fields;
    class_fields.reserve(chunk_.class_field_counts.size());
    std::vector<std::vector<Value>> arrays;

    while (!frames_.empty()) {
        Frame &cur = frames_.back();
        const FunctionProto &curf = chunk_.functions[cur.func_id];
        if (cur.ip >= chunk_.code.size()) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "instruction pointer out of range";
            e.function = chunk_.functions[cur.func_id].name;
            e.pc = static_cast<int>(cur.ip);
            return e;
        }

        Op op = static_cast<Op>(chunk_.code[cur.ip++]);
        if (op == Op::LoadIConst) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadIConst operand";
                return e;
            }
            std::uint16_t idx = ReadU16(chunk_.code, &cur.ip);
            if (idx >= chunk_.iconst.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "iconst index out of range";
                return e;
            }
            Value v;
            v.tag = ValueTag::Int64;
            v.as.i64 = chunk_.iconst[idx];
            value_stack_.push_back(v);
            continue;
        }

        if (op == Op::NewArray) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated NewArray operand";
                return e;
            }
            std::uint16_t n = ReadU16(chunk_.code, &cur.ip);
            if (value_stack_.size() < cur.stack_top + n) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "NewArray stack underflow";
                return e;
            }
            std::vector<Value> arr(n, Value::Nil());
            for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
                arr[static_cast<std::size_t>(i)] = value_stack_.back();
                value_stack_.pop_back();
            }
            arrays.push_back(std::move(arr));
            Value v;
            v.tag = ValueTag::ObjRef;
            v.as.obj = reinterpret_cast<void *>(static_cast<std::uintptr_t>(arrays.size()));
            value_stack_.push_back(v);
            continue;
        }

        if (op == Op::Index) {
            if (value_stack_.size() < cur.stack_top + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Index requires container + key";
                return e;
            }
            Value key = value_stack_.back();
            value_stack_.pop_back();
            Value arrv = value_stack_.back();
            value_stack_.pop_back();
            if (arrv.tag != ValueTag::ObjRef || key.tag != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Index expects array object + Int64 key";
                return e;
            }
            std::size_t arr_id = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(arrv.as.obj));
            if (arr_id == 0 || arr_id > arrays.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "array handle out of range";
                return e;
            }
            std::vector<Value> &arr = arrays[arr_id - 1];
            if (key.as.i64 < 0 || static_cast<std::size_t>(key.as.i64) >= arr.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::BoundsError;
                e.message = "array index out of range";
                return e;
            }
            value_stack_.push_back(arr[static_cast<std::size_t>(key.as.i64)]);
            continue;
        }

        if (op == Op::Pop) {
            if (value_stack_.size() <= cur.stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Pop requires one operand";
                return e;
            }
            value_stack_.pop_back();
            continue;
        }

        if (op == Op::Dup) {
            if (value_stack_.size() <= cur.stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Dup requires one operand";
                return e;
            }
            value_stack_.push_back(value_stack_.back());
            continue;
        }

        if (op == Op::NewClass) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated NewClass operand";
                return e;
            }
            std::uint16_t class_idx = ReadU16(chunk_.code, &cur.ip);
            if (class_idx >= chunk_.class_field_counts.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class index out of range";
                return e;
            }
            class_fields.push_back(std::vector<Value>(chunk_.class_field_counts[class_idx], Value::Nil()));
            Value v;
            v.tag = ValueTag::ObjRef;
            v.as.obj = reinterpret_cast<void *>(static_cast<std::uintptr_t>(class_fields.size()));
            value_stack_.push_back(v);
            continue;
        }

        if (op == Op::SetClassField) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated SetClassField operand";
                return e;
            }
            std::uint16_t field_idx = ReadU16(chunk_.code, &cur.ip);
            if (value_stack_.size() < cur.stack_top + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "SetClassField requires value + class";
                return e;
            }
            Value clazz = value_stack_.back(); value_stack_.pop_back();
            Value val = value_stack_.back(); value_stack_.pop_back();
            if (clazz.tag != ValueTag::ObjRef) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "SetClassField expects class object";
                return e;
            }
            std::size_t cls_id = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(clazz.as.obj));
            if (cls_id == 0 || cls_id > class_fields.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class handle out of range";
                return e;
            }
            std::vector<Value> &fields = class_fields[cls_id - 1];
            if (field_idx >= fields.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class field index out of range";
                return e;
            }
            fields[field_idx] = val;
            continue;
        }

        if (op == Op::LoadClassField) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadClassField operand";
                return e;
            }
            std::uint16_t field_idx = ReadU16(chunk_.code, &cur.ip);
            if (value_stack_.size() <= cur.stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "LoadClassField requires class object";
                return e;
            }
            Value clazz = value_stack_.back(); value_stack_.pop_back();
            if (clazz.tag != ValueTag::ObjRef) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "LoadClassField expects class object";
                return e;
            }
            std::size_t cls_id = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(clazz.as.obj));
            if (cls_id == 0 || cls_id > class_fields.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class handle out of range";
                return e;
            }
            std::vector<Value> &fields = class_fields[cls_id - 1];
            if (field_idx >= fields.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class field index out of range";
                return e;
            }
            value_stack_.push_back(fields[field_idx]);
            continue;
        }

        if (op == Op::LoadLocal) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadLocal operand";
                return e;
            }
            std::uint16_t idx = ReadU16(chunk_.code, &cur.ip);
            std::uint32_t slot = cur.base + idx;
            if (slot >= value_stack_.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                return e;
            }
            value_stack_.push_back(value_stack_[slot]);
            continue;
        }

        if (op == Op::StoreLocal) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated StoreLocal operand";
                return e;
            }
            std::uint16_t idx = ReadU16(chunk_.code, &cur.ip);
            std::uint32_t slot = cur.base + idx;
            if (slot >= value_stack_.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                return e;
            }
            if (value_stack_.size() <= cur.stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreLocal requires one operand";
                return e;
            }
            value_stack_[slot] = value_stack_.back();
            value_stack_.pop_back();
            continue;
        }

        if (op == Op::Jump) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Jump operand";
                return e;
            }
            std::int16_t rel = ReadI16(chunk_.code, &cur.ip);
            std::int64_t next_ip = static_cast<std::int64_t>(cur.ip) + rel;
            if (next_ip < static_cast<std::int64_t>(curf.code_start) ||
                next_ip >= static_cast<std::int64_t>(curf.code_end)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "jump target out of function range";
                return e;
            }
            cur.ip = static_cast<std::uint32_t>(next_ip);
            continue;
        }

        if (op == Op::JumpIfFalse) {
            if (cur.ip + 2 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated JumpIfFalse operand";
                return e;
            }
            if (value_stack_.size() <= cur.stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "JumpIfFalse requires condition";
                return e;
            }
            Value cond = value_stack_.back();
            value_stack_.pop_back();
            std::int16_t rel = ReadI16(chunk_.code, &cur.ip);
            if (!IsTruthy(cond)) {
                std::int64_t next_ip = static_cast<std::int64_t>(cur.ip) + rel;
                if (next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end)) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "jump target out of function range";
                    return e;
                }
                cur.ip = static_cast<std::uint32_t>(next_ip);
            }
            continue;
        }

        if (op == Op::CallValue) {
            if (cur.ip + 4 > chunk_.code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallValue operands";
                return e;
            }
            std::uint16_t fid = ReadU16(chunk_.code, &cur.ip);
            std::uint16_t argc = ReadU16(chunk_.code, &cur.ip);
            if (fid >= chunk_.functions.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "call target out of range";
                return e;
            }
            const FunctionProto &callee = chunk_.functions[fid];
            if (frames_.size() >= 4096) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InternalError;
                e.message = "call stack limit exceeded";
                return e;
            }
            if (argc != callee.arity) {
                RuntimeError e;
                e.code = RuntimeErrorCode::ArityMismatch;
                e.message = "call argc mismatch";
                return e;
            }
            if (value_stack_.size() < cur.stack_top + argc) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "CallValue missing arguments";
                return e;
            }

            const std::uint32_t callee_base = static_cast<std::uint32_t>(value_stack_.size() - argc);
            const std::size_t local_slots = static_cast<std::size_t>(callee.nlocals > callee.arity ? (callee.nlocals - callee.arity) : 0);
            for (std::size_t i = 0; i < local_slots; ++i) {
                value_stack_.push_back(Value::Nil());
            }

            Frame next;
            next.func_id = fid;
            next.ip = callee.code_start;
            next.base = callee_base;
            next.stack_top = next.base + callee.nlocals;
            frames_.push_back(next);
            continue;
        }

        if (op == Op::Add) {
            if (value_stack_.size() < cur.stack_top + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Add requires two operands";
                return e;
            }
            Value rhs = value_stack_.back();
            value_stack_.pop_back();
            Value lhs = value_stack_.back();
            value_stack_.pop_back();
            if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Add currently supports Int64 only";
                return e;
            }
            Value out;
            out.tag = ValueTag::Int64;
            out.as.i64 = lhs.as.i64 + rhs.as.i64;
            value_stack_.push_back(out);
            continue;
        }

        if (op == Op::Sub || op == Op::Eq || op == Op::Neq || op == Op::Gt ||
            op == Op::Gte || op == Op::Lt || op == Op::Lte || op == Op::LogicAnd || op == Op::LogicOr ||
            op == Op::Mul || op == Op::Div || op == Op::Mod) {
            if (value_stack_.size() < cur.stack_top + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "binary op requires two operands";
                return e;
            }
            Value rhs = value_stack_.back();
            value_stack_.pop_back();
            Value lhs = value_stack_.back();
            value_stack_.pop_back();
            if (lhs.tag != ValueTag::Int64 || rhs.tag != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "binary op currently supports Int64 only";
                return e;
            }
            Value out;
            out.tag = ValueTag::Int64;
            if (op == Op::Sub) out.as.i64 = lhs.as.i64 - rhs.as.i64;
            else if (op == Op::Mul) out.as.i64 = lhs.as.i64 * rhs.as.i64;
            else if (op == Op::Div) {
                if (rhs.as.i64 == 0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "division by zero";
                    return e;
                }
                out.as.i64 = lhs.as.i64 / rhs.as.i64;
            }
            else if (op == Op::Mod) {
                if (rhs.as.i64 == 0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "mod by zero";
                    return e;
                }
                out.as.i64 = lhs.as.i64 % rhs.as.i64;
            }
            else if (op == Op::Eq) out.as.i64 = lhs.as.i64 == rhs.as.i64;
            else if (op == Op::Neq) out.as.i64 = lhs.as.i64 != rhs.as.i64;
            else if (op == Op::Gt) out.as.i64 = lhs.as.i64 > rhs.as.i64;
            else if (op == Op::Gte) out.as.i64 = lhs.as.i64 >= rhs.as.i64;
            else if (op == Op::Lt) out.as.i64 = lhs.as.i64 < rhs.as.i64;
            else if (op == Op::Lte) out.as.i64 = lhs.as.i64 <= rhs.as.i64;
            else if (op == Op::LogicAnd) out.as.i64 = (lhs.as.i64 != 0 && rhs.as.i64 != 0);
            else out.as.i64 = (lhs.as.i64 != 0 || rhs.as.i64 != 0);
            value_stack_.push_back(out);
            continue;
        }

        if (op == Op::LogicNot) {
            if (value_stack_.size() <= cur.stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "logic not requires one operand";
                return e;
            }
            Value v = value_stack_.back();
            value_stack_.pop_back();
            if (v.tag != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "LogicNot currently supports Int64 only";
                return e;
            }
            Value out;
            out.tag = ValueTag::Int64;
            out.as.i64 = (v.as.i64 == 0) ? 1 : 0;
            value_stack_.push_back(out);
            continue;
        }

        if (op == Op::Return) {
            const std::uint32_t base = cur.base;
            const std::uint32_t stack_top = cur.stack_top;
            if (value_stack_.size() <= cur.stack_top) {
                last_result_ = Value::Nil();
            } else {
                last_result_ = value_stack_.back();
                value_stack_.pop_back();
            }

            frames_.pop_back();

            if (value_stack_.size() > base) {
                value_stack_.resize(base);
            }
            if (!frames_.empty()) {
                value_stack_.push_back(last_result_);
            }
            continue;
        }

        RuntimeError e;
        e.code = RuntimeErrorCode::InvalidOpcode;
        e.message = "unknown NextVM opcode";
        e.function = chunk_.functions[cur.func_id].name;
        e.pc = static_cast<int>(cur.ip - 1);
        return e;
    }

    return {};
}

} // namespace nextvm
} // namespace lpc
