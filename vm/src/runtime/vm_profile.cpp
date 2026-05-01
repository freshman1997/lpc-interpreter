#include "vm/runtime/vm.h"
#include "lpc/bytecode/opcode.h"

#include <algorithm>
#include <iomanip>
#include <vector>

using namespace lpc::vm;

static const char *OpcodeName(std::uint8_t op) {
    switch (static_cast<lpc::Op>(op)) {
    case lpc::Op::LoadIConst:   return "LoadIConst";
    case lpc::Op::LoadFConst:   return "LoadFConst";
    case lpc::Op::LoadSConst:   return "LoadSConst";
    case lpc::Op::Add:          return "Add";
    case lpc::Op::Sub:          return "Sub";
    case lpc::Op::Mul:          return "Mul";
    case lpc::Op::Div:          return "Div";
    case lpc::Op::Mod:          return "Mod";
    case lpc::Op::Neg:          return "Neg";
    case lpc::Op::Inc:          return "Inc";
    case lpc::Op::Dec:          return "Dec";
    case lpc::Op::Shl:          return "Shl";
    case lpc::Op::Shr:          return "Shr";
    case lpc::Op::BitAnd:       return "BitAnd";
    case lpc::Op::BitOr:        return "BitOr";
    case lpc::Op::BitXor:       return "BitXor";
    case lpc::Op::BitNot:       return "BitNot";
    case lpc::Op::Eq:           return "Eq";
    case lpc::Op::Neq:          return "Neq";
    case lpc::Op::Gt:           return "Gt";
    case lpc::Op::Gte:          return "Gte";
    case lpc::Op::Lt:           return "Lt";
    case lpc::Op::Lte:          return "Lte";
    case lpc::Op::LogicAnd:     return "LogicAnd";
    case lpc::Op::LogicOr:      return "LogicOr";
    case lpc::Op::LogicNot:     return "LogicNot";
    case lpc::Op::Return:       return "Return";
    case lpc::Op::LoadLocal:    return "LoadLocal";
    case lpc::Op::StoreLocal:   return "StoreLocal";
    case lpc::Op::LoadGlobal:   return "LoadGlobal";
    case lpc::Op::StoreGlobal:  return "StoreGlobal";
    case lpc::Op::LoadUpvalue:  return "LoadUpvalue";
    case lpc::Op::StoreUpvalue: return "StoreUpvalue";
    case lpc::Op::Jump:         return "Jump";
    case lpc::Op::JumpIfFalse:  return "JumpIfFalse";
    case lpc::Op::JumpIfTrue:   return "JumpIfTrue";
    case lpc::Op::CallDirect:   return "CallDirect";
    case lpc::Op::CallValue:    return "CallValue";
    case lpc::Op::CallIntrinsic:return "CallIntrinsic";
    case lpc::Op::CallVirtual:  return "CallVirtual";
    case lpc::Op::LoadFunc:     return "LoadFunc";
    case lpc::Op::NewClass:     return "NewClass";
    case lpc::Op::SetClassField:return "SetClassField";
    case lpc::Op::LoadClassField:return "LoadClassField";
    case lpc::Op::NewArray:     return "NewArray";
    case lpc::Op::NewMapping:   return "NewMapping";
    case lpc::Op::Index:        return "Index";
    case lpc::Op::StoreIndex:   return "StoreIndex";
    case lpc::Op::Upset:        return "Upset";
    case lpc::Op::SubArr:       return "SubArr";
    case lpc::Op::Pop:          return "Pop";
    case lpc::Op::Dup:          return "Dup";
    case lpc::Op::ForeachStep1: return "ForeachStep1";
    case lpc::Op::ForeachStep2: return "ForeachStep2";
    case lpc::Op::Catch:        return "Catch";
    case lpc::Op::Switch:       return "Switch";
    case lpc::Op::IncLocal:     return "IncLocal";
    case lpc::Op::DecLocal:     return "DecLocal";
    case lpc::Op::JumpIfLocalLtFalse:return "JumpIfLocalLtFalse";
    case lpc::Op::AddLocalLocalToLocal:return "AddLocalLocalToLocal";
    case lpc::Op::IncLocalAndJump:return "IncLocalAndJump";
    case lpc::Op::JumpIfLocalIConstLteFalse:return "JumpIfLocalIConstLteFalse";
    case lpc::Op::LoadLocalDec:return "LoadLocalDec";
    case lpc::Op::LoadLocalSubIConst:return "LoadLocalSubIConst";
    case lpc::Op::LoadLocalAddIConst:return "LoadLocalAddIConst";
    case lpc::Op::AddLocalIndexIConstToLocal:return "AddLocalIndexIConstToLocal";
    case lpc::Op::AddLocalIndexLocalToLocal:return "AddLocalIndexLocalToLocal";
    case lpc::Op::AddLocalLocalIncJumpIfLocalLt:return "AddLocalLocalIncJumpIfLocalLt";
    case lpc::Op::AddLocalIConstToLocal:return "AddLocalIConstToLocal";
    case lpc::Op::SubLocalIConstToLocal:return "SubLocalIConstToLocal";
    default:                    return "Unknown";
    }
}

void Vm::PrintProfile(std::ostream &os) const {
    if (!profile_enabled_) return;

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        profile_end_ - profile_start_).count();
    const double elapsed_ms = static_cast<double>(elapsed_us) / 1000.0;
    const double mips = elapsed_us > 0
        ? (static_cast<double>(instruction_count_) / static_cast<double>(elapsed_us))
        : 0.0;

    os << "[profile] elapsed_ms=" << std::fixed << std::setprecision(3) << elapsed_ms
       << " instr=" << instruction_count_
       << " MIPS=" << std::setprecision(3) << mips << "\n";

    std::vector<std::pair<std::uint8_t, std::uint64_t>> top;
    top.reserve(256);
    for (std::size_t i = 0; i < opcode_counts_.size(); ++i) {
        if (opcode_counts_[i] > 0) {
            top.emplace_back(static_cast<std::uint8_t>(i), opcode_counts_[i]);
        }
    }

    std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    const std::size_t n = std::min<std::size_t>(top.size(), 12);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t op = top[i].first;
        const std::uint64_t cnt = top[i].second;
        const double pct = instruction_count_ > 0
            ? (100.0 * static_cast<double>(cnt) / static_cast<double>(instruction_count_))
            : 0.0;
        os << "[profile] " << OpcodeName(op)
           << " count=" << cnt
           << " pct=" << std::setprecision(2) << pct << "\n";
    }
}
