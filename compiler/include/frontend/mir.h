#ifndef LPC_FRONTEND_MIR_H
#define LPC_FRONTEND_MIR_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lpc {
namespace frontend {

enum class MirOp {
    LoadConst,
    LoadFConst,
    LoadSConst,
    LoadFunction,
    LoadLocal,
    StoreLocal,
    LoadUpvalue,
    StoreUpvalue,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Shl,
    Shr,
    BitAnd,
    BitOr,
    BitXor,
    LogicAnd,
    LogicOr,
    LogicNot,
    BitNot,
    Neg,
    Lt,
    Lte,
    Gt,
    Gte,
    Eq,
    Neq,
    Index,
    StoreIndex,
    NewArray,
    NewClass,
    LoadClassField,
    StoreClassField,
    Call,
    CallEfun,
    ForeachInit,
    ForeachNext,
    JumpIfFalse,
    JumpIfTrue,
    Jump,
    Return,
    Pop,
    Dup,
    NewMapping,
    Catch,
    LoadGlobal,
    StoreGlobal,
    Upset,
};

struct MirInstr {
    MirOp op = MirOp::LoadConst;
    int a = 0;
    int b = 0;
    int line = 0;
};

struct MirFunction {
    std::string name;
    int nargs = 0;
    std::vector<std::string> locals;
    std::vector<std::string> upvalues;
    std::vector<int> upvalue_source_kind;
    std::vector<int> upvalue_source_index;
    std::vector<std::int64_t> iconsts;
    std::vector<double> fconsts;
    std::vector<std::string> sconsts;
    std::vector<MirInstr> code;
    int max_stack = 0;
};

struct MirModule {
    std::vector<MirFunction> functions;
    std::vector<std::string> global_variables;
    MirFunction init_function;
    std::vector<std::string> class_order;
    std::unordered_map<std::string, std::vector<std::string>> class_fields;
    std::unordered_map<std::string, std::string> class_parent;
};

} // namespace frontend
} // namespace lpc

#endif
