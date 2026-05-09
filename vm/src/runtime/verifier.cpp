#include "vm/bytecode/verifier.h"

#include <cstdint>
#include <vector>

#include "lpc/bytecode/opcode.h"

namespace {

using lpc::vm::Op;

static std::uint16_t ReadU16(const std::vector<std::uint8_t> &code, std::uint32_t *ip) {
    const std::uint32_t i = *ip;
    std::uint16_t v = static_cast<std::uint16_t>(code[i + 1] << 8) | static_cast<std::uint16_t>(code[i]);
    *ip += 2;
    return v;
}

static std::int16_t ReadI16(const std::vector<std::uint8_t> &code, std::uint32_t *ip) {
    return static_cast<std::int16_t>(ReadU16(code, ip));
}

static bool IsBinaryArith(Op op) {
    return op == Op::Add || op == Op::Sub || op == Op::Mul || op == Op::Div || op == Op::Mod ||
           op == Op::Shl || op == Op::Shr || op == Op::BitAnd || op == Op::BitOr || op == Op::BitXor;
}

static bool IsCompare(Op op) {
    return op == Op::Eq || op == Op::Neq || op == Op::Gt || op == Op::Gte || op == Op::Lt || op == Op::Lte;
}

static bool IsLogicBinary(Op op) {
    return op == Op::LogicAnd || op == Op::LogicOr;
}

static bool IsUnary(Op op) {
    return op == Op::Neg || op == Op::Inc || op == Op::Dec || op == Op::BitNot || op == Op::LogicNot;
}

static bool NeedsU16(Op op) {
    return op == Op::LoadIConst || op == Op::LoadFConst || op == Op::LoadSConst ||
           op == Op::LoadLocal || op == Op::StoreLocal ||
           op == Op::IncLocal || op == Op::DecLocal ||
           op == Op::LoadGlobal || op == Op::StoreGlobal ||
           op == Op::LoadUpvalue || op == Op::StoreUpvalue ||
           op == Op::NewClass || op == Op::SetClassField || op == Op::LoadClassField ||
           op == Op::NewArray || op == Op::NewMapping ||
           op == Op::LoadFunc;
}

static bool NeedsRel16(Op op) {
    return op == Op::Jump || op == Op::JumpIfFalse || op == Op::JumpIfTrue;
}

static bool IsLocalCompareJump(Op op) {
    return op == Op::JumpIfLocalLtFalse || op == Op::JumpIfLocalIConstLteFalse;
}

// 中文说明：下面这些分类只服务字节码校验器。
// superinstruction 的栈效果和跳转目标必须在加载阶段验证，避免 VM 热路径重复做昂贵检查。
static bool IsLocalLocalToLocal(Op op) {
    return op == Op::AddLocalLocalToLocal ||
           op == Op::AddLocalIConstToLocal ||
           op == Op::SubLocalIConstToLocal ||
           op == Op::BitAndLocalIConstToLocal ||
           op == Op::BitOrLocalIConstToLocal ||
           op == Op::BitXorLocalIConstToLocal ||
           op == Op::ShlLocalIConstToLocal ||
           op == Op::ShrLocalIConstToLocal ||
           op == Op::AddLocalFConstToLocal ||
           op == Op::SubLocalFConstToLocal ||
           op == Op::MulLocalFConstToLocal ||
           op == Op::DivLocalFConstToLocal;
}

static bool IsLocalJump(Op op) {
    return op == Op::IncLocalAndJump;
}

static bool IsLocalExpr(Op op) {
    return op == Op::LoadLocalDec || op == Op::LoadLocalSubIConst || op == Op::LoadLocalAddIConst ||
           op == Op::LoadLocalBitAndIConst ||
           op == Op::LoadLocalAddFConst || op == Op::LoadLocalSubFConst ||
           op == Op::LoadLocalMulFConst || op == Op::LoadLocalDivFConst ||
           op == Op::LoadLocalDupAddFConst || op == Op::LoadLocalDupSubFConst ||
           op == Op::LoadLocalDupMulFConst || op == Op::LoadLocalDupDivFConst ||
           op == Op::LoadLocalClassField;
}

static bool IsLocalIndexAccum(Op op) {
    return op == Op::AddLocalIndexIConstToLocal || op == Op::AddLocalIndexLocalToLocal;
}

static bool IsLocalClassField(Op op) {
    return op == Op::SetClassFieldLocalFromLocal || op == Op::AddLocalClassFieldToLocal ||
           op == Op::AddLocalTwoClassFieldsToLocal;
}

static bool IsClosureLocalAccum(Op op) {
    return op == Op::AddLocalToUpvalueAndLoad;
}

static bool IsLocalLoopTail(Op op) {
    return op == Op::AddLocalLocalIncJumpIfLocalLt;
}

} // namespace

namespace lpc {
namespace vm {

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

        if (NeedsU16(op)) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated u16 operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t idx = ReadU16(chunk.code, &ip);

            if (op == Op::LoadIConst) {
                if (idx >= chunk.iconst.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "iconst index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                ++depth;
            } else if (op == Op::LoadFConst) {
                if (idx >= chunk.fconst.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "fconst index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                ++depth;
            } else if (op == Op::LoadSConst) {
                if (idx >= chunk.sconst.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "sconst index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                ++depth;
            } else if (op == Op::LoadLocal || op == Op::LoadGlobal || op == Op::LoadUpvalue || op == Op::LoadFunc) {
                ++depth;
            } else if (op == Op::IncLocal || op == Op::DecLocal) {
                // stack unchanged
            } else if (op == Op::StoreLocal || op == Op::StoreGlobal || op == Op::StoreUpvalue) {
                if (depth < 1) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "store op stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                --depth;
            } else if (op == Op::NewClass) {
                if (idx >= chunk.classes.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "class index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                ++depth;
            } else if (op == Op::SetClassField) {
                if (depth < 2) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "SetClassField stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                depth -= 2;
            } else if (op == Op::LoadClassField) {
                if (depth < 1) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "LoadClassField stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            } else if (op == Op::NewArray) {
                if (depth < static_cast<int>(idx)) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "NewArray stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                depth = depth - static_cast<int>(idx) + 1;
            } else if (op == Op::NewMapping) {
                int npairs = static_cast<int>(idx);
                if (depth < npairs * 2) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "NewMapping stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                depth = depth - npairs * 2 + 1;
            }
            continue;
        }

        if (NeedsRel16(op)) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated jump operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (op == Op::JumpIfFalse || op == Op::JumpIfTrue) {
                if (depth < 1) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "conditional jump stack underflow";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                --depth;
            }
            std::int16_t rel = ReadI16(chunk.code, &ip);
            std::int64_t target = static_cast<std::int64_t>(ip) + rel;
            if (target < static_cast<std::int64_t>(f.code_start) ||
                target >= static_cast<std::int64_t>(f.code_end)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "jump target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (IsLocalCompareJump(op)) {
            if (ip + 6 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated local compare jump operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t lhs = ReadU16(chunk.code, &ip);
            std::uint16_t rhs = ReadU16(chunk.code, &ip);
            bool rhs_ok = op == Op::JumpIfLocalIConstLteFalse
                ? rhs < chunk.iconst.size()
                : rhs < f.nlocals;
            if (lhs >= f.nlocals || !rhs_ok) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local compare jump local index out of range";
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
                e.message = "local compare jump target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (IsLocalLocalToLocal(op)) {
            if (ip + 6 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated local-local operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t dst = ReadU16(chunk.code, &ip);
            std::uint16_t lhs = ReadU16(chunk.code, &ip);
            std::uint16_t rhs = ReadU16(chunk.code, &ip);
            const bool rhs_is_iconst = op == Op::AddLocalIConstToLocal || op == Op::SubLocalIConstToLocal ||
                op == Op::BitAndLocalIConstToLocal || op == Op::BitOrLocalIConstToLocal ||
                op == Op::BitXorLocalIConstToLocal || op == Op::ShlLocalIConstToLocal ||
                op == Op::ShrLocalIConstToLocal;
            const bool rhs_is_fconst = op == Op::AddLocalFConstToLocal || op == Op::SubLocalFConstToLocal ||
                op == Op::MulLocalFConstToLocal || op == Op::DivLocalFConstToLocal;
            bool rhs_ok;
            if (rhs_is_iconst) rhs_ok = rhs < chunk.iconst.size();
            else if (rhs_is_fconst) rhs_ok = rhs < chunk.fconst.size();
            else rhs_ok = rhs < f.nlocals;
            if (dst >= f.nlocals || lhs >= f.nlocals || !rhs_ok) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local-local local index out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (IsLocalJump(op)) {
            if (ip + 4 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated local jump operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t idx = ReadU16(chunk.code, &ip);
            if (idx >= f.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local jump local index out of range";
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
                e.message = "local jump target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (IsLocalExpr(op)) {
            const std::uint32_t operand_size = op == Op::LoadLocalDec ? 2 : 4;
            if (ip + operand_size > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated local expression operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t local = ReadU16(chunk.code, &ip);
            if (local >= f.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local expression local index out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (op == Op::LoadLocalSubIConst || op == Op::LoadLocalAddIConst || op == Op::LoadLocalBitAndIConst) {
                std::uint16_t cidx = ReadU16(chunk.code, &ip);
                if (cidx >= chunk.iconst.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "local expression iconst index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            } else if (op == Op::LoadLocalAddFConst || op == Op::LoadLocalSubFConst ||
                       op == Op::LoadLocalMulFConst || op == Op::LoadLocalDivFConst ||
                       op == Op::LoadLocalDupAddFConst || op == Op::LoadLocalDupSubFConst ||
                       op == Op::LoadLocalDupMulFConst || op == Op::LoadLocalDupDivFConst) {
                std::uint16_t cidx = ReadU16(chunk.code, &ip);
                if (cidx >= chunk.fconst.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "local expression fconst index out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            } else if (op == Op::LoadLocalClassField) {
                ReadU16(chunk.code, &ip);
            }
            if (op == Op::LoadLocalDupAddFConst || op == Op::LoadLocalDupSubFConst ||
                op == Op::LoadLocalDupMulFConst || op == Op::LoadLocalDupDivFConst) {
                depth += 2;
            } else {
                ++depth;
            }
            continue;
        }

        if (IsLocalIndexAccum(op)) {
            if (ip + 8 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated local index accumulation operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t dst = ReadU16(chunk.code, &ip);
            std::uint16_t lhs = ReadU16(chunk.code, &ip);
            std::uint16_t container = ReadU16(chunk.code, &ip);
            std::uint16_t key = ReadU16(chunk.code, &ip);
            const bool key_ok = op == Op::AddLocalIndexIConstToLocal
                ? key < chunk.iconst.size()
                : key < f.nlocals;
            if (dst >= f.nlocals || lhs >= f.nlocals || container >= f.nlocals || !key_ok) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index accumulation operand out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (IsLocalClassField(op)) {
            if (op == Op::SetClassFieldLocalFromLocal) {
                if (ip + 6 > f.code_end) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "truncated local class-field store operands";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                std::uint16_t object_local = ReadU16(chunk.code, &ip);
                std::uint16_t value_local = ReadU16(chunk.code, &ip);
                ReadU16(chunk.code, &ip);
                if (object_local >= f.nlocals || value_local >= f.nlocals) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "local class-field store operand out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            } else if (op == Op::AddLocalClassFieldToLocal) {
                if (ip + 8 > f.code_end) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "truncated local class-field add operands";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                std::uint16_t dst = ReadU16(chunk.code, &ip);
                std::uint16_t lhs = ReadU16(chunk.code, &ip);
                std::uint16_t object_local = ReadU16(chunk.code, &ip);
                ReadU16(chunk.code, &ip);
                if (dst >= f.nlocals || lhs >= f.nlocals || object_local >= f.nlocals) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "local class-field add operand out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            } else {
                if (ip + 10 > f.code_end) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "truncated local class-field chain operands";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
                std::uint16_t dst = ReadU16(chunk.code, &ip);
                std::uint16_t lhs = ReadU16(chunk.code, &ip);
                std::uint16_t object_local = ReadU16(chunk.code, &ip);
                ReadU16(chunk.code, &ip);
                ReadU16(chunk.code, &ip);
                if (dst >= f.nlocals || lhs >= f.nlocals || object_local >= f.nlocals) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "local class-field chain operand out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            }
            continue;
        }

        if (IsClosureLocalAccum(op)) {
            if (ip + 4 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated closure local accumulation operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t up_idx = ReadU16(chunk.code, &ip);
            std::uint16_t local_idx = ReadU16(chunk.code, &ip);
            if (up_idx >= f.upvalues.size() || local_idx >= f.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "closure local accumulation operand out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ++depth;
            continue;
        }

        if (IsLocalLoopTail(op)) {
            if (ip + 14 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated local loop-tail operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            for (int k = 0; k < 6; ++k) {
                std::uint16_t local = ReadU16(chunk.code, &ip);
                if (local >= f.nlocals) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "local loop-tail operand out of range";
                    e.function = f.name;
                    e.pc = static_cast<int>(op_pc);
                    return e;
                }
            }
            std::int16_t rel = ReadI16(chunk.code, &ip);
            std::int64_t target = static_cast<std::int64_t>(ip) + rel;
            if (target < static_cast<std::int64_t>(f.code_start) ||
                target >= static_cast<std::int64_t>(f.code_end)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local loop-tail target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (op == Op::CallDirect) {
            if (ip + 4 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated call operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t callee_id = ReadU16(chunk.code, &ip);
            std::uint16_t argc = ReadU16(chunk.code, &ip);
            if (callee_id >= chunk.functions.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "call target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (argc != chunk.functions[callee_id].arity) {
                RuntimeError e;
                e.code = RuntimeErrorCode::ArityMismatch;
                e.message = "call argc mismatch";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            if (depth < static_cast<int>(argc)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "call stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth = depth - static_cast<int>(argc) + 1;
            continue;
        }

        if (op == Op::CallValue) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallValue operand";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t argc = ReadU16(chunk.code, &ip);
            if (depth < static_cast<int>(argc) + 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "CallValue stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth = depth - static_cast<int>(argc);
            continue;
        }

        if (op == Op::CallIntrinsic) {
            if (ip + 3 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallIntrinsic operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t efun_idx = ReadU16(chunk.code, &ip);
            std::uint8_t argc = chunk.code[ip++];
            bool is_void = (efun_idx == 1 || efun_idx == 2 || efun_idx == 3 ||
                            efun_idx == 13 || efun_idx == 15 || efun_idx == 31);
            if (depth < static_cast<int>(argc)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "CallIntrinsic stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth -= static_cast<int>(argc);
            if (!is_void) {
                ++depth;
            }
            continue;
        }

        if (op == Op::CallVirtual) {
            if (ip + 4 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallVirtual operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            std::uint16_t name_idx = ReadU16(chunk.code, &ip);
            std::uint16_t argc = ReadU16(chunk.code, &ip);
            if (depth < static_cast<int>(argc)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "CallVirtual stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth = depth - static_cast<int>(argc) + 1;
            continue;
        }

        if (IsBinaryArith(op) || IsCompare(op) || IsLogicBinary(op)) {
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

        if (IsUnary(op)) {
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "unary op stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (op == Op::Index || op == Op::StoreIndex) {
            int required = (op == Op::StoreIndex) ? 3 : 2;
            if (depth < required) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "index op stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth -= (op == Op::StoreIndex) ? 2 : 1;
            if (op == Op::StoreIndex) ++depth;
            continue;
        }

        if (op == Op::Upset) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Upset sub-ops";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ip += 2;
            if (depth < 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Upset stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth -= 1;
            continue;
        }

        if (op == Op::SubArr) {
            if (depth < 3) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "SubArr stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            depth -= 2;
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

        if (op == Op::ForeachStep1) {
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "ForeachStep1 stack underflow";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ++depth;
            continue;
        }

        if (op == Op::ForeachStep2) {
            if (ip + 7 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated ForeachStep2 operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ip += 7;
            continue;
        }

        if (op == Op::Catch) {
            if (ip + 2 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Catch operand";
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
                e.message = "Catch target out of range";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            continue;
        }

        if (op == Op::Switch) {
            if (ip + 6 > f.code_end) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Switch operands";
                e.function = f.name;
                e.pc = static_cast<int>(op_pc);
                return e;
            }
            ip += 6;
            if (depth < 1) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Switch stack underflow";
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
        e.message = "unknown NextVM opcode: " + std::to_string(static_cast<int>(op));
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

} // namespace vm
} // namespace lpc
