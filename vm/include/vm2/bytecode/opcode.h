#ifndef LPC_VM2_BYTECODE_OPCODE_H
#define LPC_VM2_BYTECODE_OPCODE_H

#include <cstdint>

namespace lpc {
namespace vm2 {

enum class Op : std::uint8_t {
    LoadIConst = 1,
    Add = 2,
    Return = 3,
    LoadLocal = 4,
    StoreLocal = 5,
    Jump = 6,
    JumpIfFalse = 7,
    CallValue = 8,
    Sub = 9,
    Eq = 10,
    Neq = 11,
    Gt = 12,
    Gte = 13,
    Lt = 14,
    Lte = 15,
    LogicAnd = 16,
    LogicOr = 17,
    LogicNot = 18,
    Mul = 19,
    Div = 20,
    Mod = 21,
    NewClass = 22,
    SetClassField = 23,
    LoadClassField = 24,
    NewArray = 25,
    Index = 26,
};

} // namespace vm2
} // namespace lpc

#endif
