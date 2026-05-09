#include "frontend/bytecode_writer.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lpc/bytecode/opcode.h"

namespace lpc {
namespace frontend {

using Op = lpc::Op;

static void WriteU32(std::vector<std::uint8_t> &buf, std::uint32_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xff));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

static bool LowerMirToNextVm(
    const MirFunction &fn,
    std::unordered_map<std::int64_t, int> &iconst_index,
    std::vector<std::int64_t> &iconsts,
    std::unordered_map<double, int> &fconst_index,
    std::vector<double> &fconsts,
    std::unordered_map<std::string, int> &sconst_index,
    std::vector<std::string> &sconsts,
    std::vector<std::uint8_t> &out,
    std::string *err,
    std::vector<int> *mir_pc_to_byte) {
    std::unordered_map<int, int> pc_map;
    std::vector<std::pair<int, int>> patches;

    auto fail = [&](const std::string &msg) {
        if (err) {
            *err = msg;
        }
        return false;
    };
    auto emit_u8 = [&](std::uint8_t v) {
        out.push_back(v);
    };
    auto emit_u16 = [&](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xff));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto ensure_iconst = [&](std::int64_t literal) {
        auto it = iconst_index.find(literal);
        if (it != iconst_index.end()) {
            return it->second;
        }
        int idx = static_cast<int>(iconsts.size());
        iconst_index[literal] = idx;
        iconsts.push_back(literal);
        return idx;
    };
    auto emit_load_iconst = [&](std::int64_t literal) {
        int idx = ensure_iconst(literal);
        emit_u8(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(static_cast<std::uint16_t>(idx));
    };

    int n = static_cast<int>(fn.code.size());
    std::unordered_set<int> jump_targets;
    jump_targets.reserve(static_cast<std::size_t>(n));
    for (const auto &ins : fn.code) {
        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse ||
            ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch ||
            ins.op == MirOp::ForeachNext) {
            jump_targets.insert(ins.op == MirOp::ForeachNext ? ins.b : ins.a);
        }
    }
    for (int i = 0; i < n; ++i) {
        pc_map[i] = static_cast<int>(out.size());
        if (mir_pc_to_byte) {
            mir_pc_to_byte->push_back(static_cast<int>(out.size()));
        }
        const MirInstr &mi = fn.code[i];
        // 中文说明：这里在 MIR 降到 NextVM 字节码时做轻量 peephole。
        // 只有中间指令不是跳转目标时才合成，避免改变控制流入口和调试断点语义。
        // 规整 while 循环：先保留一次进入循环的 guard，再把 body + i++ + 回跳条件合成。
        if (i + 9 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::Lt &&
            fn.code[i + 3].op == MirOp::JumpIfFalse &&
            fn.code[i + 4].op == MirOp::LoadLocal &&
            fn.code[i + 5].op == MirOp::LoadLocal &&
            fn.code[i + 6].op == MirOp::Add &&
            fn.code[i + 7].op == MirOp::StoreLocal &&
            fn.code[i + 8].op == MirOp::IncLocal &&
            fn.code[i + 9].op == MirOp::Jump &&
            fn.code[i + 9].a == i &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            !jump_targets.count(i + 4) &&
            !jump_targets.count(i + 5) &&
            !jump_targets.count(i + 6) &&
            !jump_targets.count(i + 7) &&
            !jump_targets.count(i + 8) &&
            !jump_targets.count(i + 9) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 4].a >= 0 && fn.code[i + 4].a <= 0xffff &&
            fn.code[i + 5].a >= 0 && fn.code[i + 5].a <= 0xffff &&
            fn.code[i + 7].a >= 0 && fn.code[i + 7].a <= 0xffff &&
            fn.code[i + 8].a >= 0 && fn.code[i + 8].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::JumpIfLocalLtFalse));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            int guard_patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({guard_patch_pos, fn.code[i + 3].a});

            const int body_byte = static_cast<int>(out.size());
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalLocalIncJumpIfLocalLt));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 7].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 4].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 5].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 8].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            int loop_patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            int loop_rel = body_byte - (loop_patch_pos + 2);
            if (loop_rel < -32768 || loop_rel > 32767) {
                return fail("nextvm loop-tail target too far");
            }
            std::uint16_t loop_bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(loop_rel));
            out[loop_patch_pos + 0] = static_cast<std::uint8_t>(loop_bits & 0xff);
            out[loop_patch_pos + 1] = static_cast<std::uint8_t>((loop_bits >> 8) & 0xff);

            for (int s = 1; s <= 9; ++s) {
                ++i;
                pc_map[i] = (s >= 4) ? body_byte : static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(pc_map[i]);
                }
            }
            continue;
        }
        // while/for 条件的热点形态：LoadLocal, LoadLocal, Lt, JumpIfFalse。
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::Lt &&
            fn.code[i + 3].op == MirOp::JumpIfFalse &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::JumpIfLocalLtFalse));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, fn.code[i + 3].a});
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // 递归或边界判断的热点形态：LoadLocal, LoadConst, Lte, JumpIfFalse。
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadConst &&
            fn.code[i + 2].op == MirOp::Lte &&
            fn.code[i + 3].op == MirOp::JumpIfFalse &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.iconsts.size())) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 1].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::JumpIfLocalIConstLteFalse));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, fn.code[i + 3].a});
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // 局部变量加法赋值：LoadLocal, LoadLocal, Add, StoreLocal。
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::Add &&
            fn.code[i + 3].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 3].a >= 0 && fn.code[i + 3].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalLocalToLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 3].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadConst &&
            (fn.code[i + 2].op == MirOp::Add || fn.code[i + 2].op == MirOp::Sub) &&
            fn.code[i + 3].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.iconsts.size()) &&
            fn.code[i + 3].a >= 0 && fn.code[i + 3].a <= 0xffff) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 1].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            emit_u8(static_cast<std::uint8_t>(
                fn.code[i + 2].op == MirOp::Add ? Op::AddLocalIConstToLocal : Op::SubLocalIConstToLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 3].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Bitwise/shift local-iconst-store: a = a & 0xFF, a = a | 0x10, etc.
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadConst &&
            (fn.code[i + 2].op == MirOp::BitAnd || fn.code[i + 2].op == MirOp::BitOr ||
             fn.code[i + 2].op == MirOp::BitXor || fn.code[i + 2].op == MirOp::Shl ||
             fn.code[i + 2].op == MirOp::Shr) &&
            fn.code[i + 3].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.iconsts.size()) &&
            fn.code[i + 3].a >= 0 && fn.code[i + 3].a <= 0xffff) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 1].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            Op bit_op;
            switch (fn.code[i + 2].op) {
            case MirOp::BitAnd: bit_op = Op::BitAndLocalIConstToLocal; break;
            case MirOp::BitOr:  bit_op = Op::BitOrLocalIConstToLocal; break;
            case MirOp::BitXor: bit_op = Op::BitXorLocalIConstToLocal; break;
            case MirOp::Shl:    bit_op = Op::ShlLocalIConstToLocal; break;
            case MirOp::Shr:    bit_op = Op::ShrLocalIConstToLocal; break;
            default:            bit_op = Op::BitAndLocalIConstToLocal; break;
            }
            emit_u8(static_cast<std::uint8_t>(bit_op));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 3].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Float arithmetic local-fconst-store: sum = sum + 1.0, pos = pos * 2.0, etc.
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadFConst &&
            (fn.code[i + 2].op == MirOp::Add || fn.code[i + 2].op == MirOp::Sub ||
             fn.code[i + 2].op == MirOp::Mul || fn.code[i + 2].op == MirOp::Div) &&
            fn.code[i + 3].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.fconsts.size()) &&
            fn.code[i + 3].a >= 0 && fn.code[i + 3].a <= 0xffff) {
            double fval = fn.fconsts[fn.code[i + 1].a];
            int fidx = 0;
            auto fit = fconst_index.find(fval);
            if (fit != fconst_index.end()) {
                fidx = fit->second;
            } else {
                fidx = static_cast<int>(fconsts.size());
                fconst_index[fval] = fidx;
                fconsts.push_back(fval);
            }
            if (fidx > 0xffff) {
                return fail("nextvm lowering fconst index out of range");
            }
            Op fop;
            switch (fn.code[i + 2].op) {
            case MirOp::Add: fop = Op::AddLocalFConstToLocal; break;
            case MirOp::Sub: fop = Op::SubLocalFConstToLocal; break;
            case MirOp::Mul: fop = Op::MulLocalFConstToLocal; break;
            case MirOp::Div: fop = Op::DivLocalFConstToLocal; break;
            default:         fop = Op::AddLocalFConstToLocal; break;
            }
            emit_u8(static_cast<std::uint8_t>(fop));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 3].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fidx));
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Class field store from local: v->x = i / obj->field = value。
        if (i + 2 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::StoreClassField &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 2].a >= 0 && fn.code[i + 2].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::SetClassFieldLocalFromLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 2].a));
            for (int s = 0; s < 2; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Class field add accumulation: sum = sum + obj->field。
        if (i + 4 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::LoadClassField &&
            fn.code[i + 3].op == MirOp::Add &&
            fn.code[i + 4].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            !jump_targets.count(i + 4) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 2].a >= 0 && fn.code[i + 2].a <= 0xffff &&
            fn.code[i + 4].a >= 0 && fn.code[i + 4].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalClassFieldToLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 4].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 2].a));
            for (int s = 0; s < 4; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Class field chain accumulation: sum = sum + obj->f1 + obj->f2。
        if (i + 7 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::LoadClassField &&
            fn.code[i + 3].op == MirOp::Add &&
            fn.code[i + 4].op == MirOp::LoadLocal &&
            fn.code[i + 5].op == MirOp::LoadClassField &&
            fn.code[i + 6].op == MirOp::Add &&
            fn.code[i + 7].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            !jump_targets.count(i + 4) &&
            !jump_targets.count(i + 5) &&
            !jump_targets.count(i + 6) &&
            !jump_targets.count(i + 7) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 2].a >= 0 && fn.code[i + 2].a <= 0xffff &&
            fn.code[i + 4].a == fn.code[i + 1].a &&
            fn.code[i + 5].a >= 0 && fn.code[i + 5].a <= 0xffff &&
            fn.code[i + 7].a >= 0 && fn.code[i + 7].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalTwoClassFieldsToLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 7].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 2].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 5].a));
            for (int s = 0; s < 7; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Closure update + return: upvalue = upvalue + local; return upvalue.
        if (i + 5 < n &&
            mi.op == MirOp::LoadUpvalue &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::Add &&
            fn.code[i + 3].op == MirOp::StoreUpvalue &&
            fn.code[i + 4].op == MirOp::LoadUpvalue &&
            fn.code[i + 5].op == MirOp::Return &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            !jump_targets.count(i + 4) &&
            !jump_targets.count(i + 5) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 3].a == mi.a &&
            fn.code[i + 4].a == mi.a) {
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalToUpvalueAndLoad));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            emit_u8(static_cast<std::uint8_t>(Op::Return));
            for (int s = 0; s < 5; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Class assignment result unused: obj->field = value; (discard assignment value)
        // Pattern emitted by front-end: StoreClassField; LoadLocal(obj); LoadClassField(field); Pop.
        if (i >= 1 && i + 3 < n &&
            mi.op == MirOp::StoreClassField &&
            fn.code[i - 1].op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::LoadClassField &&
            fn.code[i + 3].op == MirOp::Pop &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i - 1].a >= 0 && fn.code[i - 1].a <= 0xffff &&
            fn.code[i + 1].a == fn.code[i - 1].a &&
            fn.code[i + 2].a == mi.a) {
            emit_u8(static_cast<std::uint8_t>(Op::SetClassField));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // 局部变量与整数常量的加减赋值：hp = hp + 10 / cd = cd - 1。
        // 循环尾部：IncLocal 后立刻 Jump。
        if (i + 5 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::LoadConst &&
            fn.code[i + 3].op == MirOp::Index &&
            fn.code[i + 4].op == MirOp::Add &&
            fn.code[i + 5].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            !jump_targets.count(i + 4) &&
            !jump_targets.count(i + 5) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 2].a >= 0 &&
            fn.code[i + 2].a < static_cast<int>(fn.iconsts.size()) &&
            fn.code[i + 5].a >= 0 && fn.code[i + 5].a <= 0xffff) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 2].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalIndexIConstToLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 5].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            for (int s = 0; s < 5; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        if (i + 5 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadLocal &&
            fn.code[i + 2].op == MirOp::LoadLocal &&
            fn.code[i + 3].op == MirOp::Index &&
            fn.code[i + 4].op == MirOp::Add &&
            fn.code[i + 5].op == MirOp::StoreLocal &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            !jump_targets.count(i + 4) &&
            !jump_targets.count(i + 5) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff &&
            fn.code[i + 2].a >= 0 && fn.code[i + 2].a <= 0xffff &&
            fn.code[i + 5].a >= 0 && fn.code[i + 5].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::AddLocalIndexLocalToLocal));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 5].a));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 2].a));
            for (int s = 0; s < 5; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // sum = sum + arr[0] / sum = sum + m[i]：把 Index、Add、StoreLocal 收进单条指令。
        if (i + 1 < n &&
            mi.op == MirOp::IncLocal &&
            fn.code[i + 1].op == MirOp::Jump &&
            !jump_targets.count(i + 1) &&
            mi.a >= 0 && mi.a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::IncLocalAndJump));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, fn.code[i + 1].a});
            ++i;
            pc_map[i] = static_cast<int>(out.size());
            if (mir_pc_to_byte) {
                mir_pc_to_byte->push_back(static_cast<int>(out.size()));
            }
            continue;
        }
        // 调用参数 n - 1：前端已经把 x - 1 折成 Dec，这里进一步折为单条 LoadLocalDec。
        if (i + 1 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::Dec &&
            !jump_targets.count(i + 1) &&
            mi.a >= 0 && mi.a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::LoadLocalDec));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            ++i;
            pc_map[i] = static_cast<int>(out.size());
            if (mir_pc_to_byte) {
                mir_pc_to_byte->push_back(static_cast<int>(out.size()));
            }
            continue;
        }
        // 调用参数 n - 常量：避免 LoadLocal/LoadConst/Sub 三次调度。
        if (i + 2 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadConst &&
            fn.code[i + 2].op == MirOp::Add &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.iconsts.size())) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 1].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::LoadLocalAddIConst));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            for (int s = 0; s < 2; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // 调用参数或字面量元素 n + 常量：避免 LoadLocal/LoadConst/Add 三次调度。
        if (i + 2 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadConst &&
            fn.code[i + 2].op == MirOp::Sub &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.iconsts.size())) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 1].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::LoadLocalSubIConst));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            for (int s = 0; s < 2; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Bitwise AND local & iconst: 避免三次调度。
        if (i + 2 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadConst &&
            fn.code[i + 2].op == MirOp::BitAnd &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.iconsts.size())) {
            int const_idx = ensure_iconst(fn.iconsts[fn.code[i + 1].a]);
            if (const_idx > 0xffff) {
                return fail("nextvm lowering iconst index out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::LoadLocalBitAndIConst));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(const_idx));
            for (int s = 0; s < 2; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Float local-const expression: locals[local] + fconst, result on stack.
        if (i + 2 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadFConst &&
            (fn.code[i + 2].op == MirOp::Add || fn.code[i + 2].op == MirOp::Sub ||
             fn.code[i + 2].op == MirOp::Mul || fn.code[i + 2].op == MirOp::Div) &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 &&
            fn.code[i + 1].a < static_cast<int>(fn.fconsts.size())) {
            double fval = fn.fconsts[fn.code[i + 1].a];
            int fidx = 0;
            auto fit = fconst_index.find(fval);
            if (fit != fconst_index.end()) {
                fidx = fit->second;
            } else {
                fidx = static_cast<int>(fconsts.size());
                fconst_index[fval] = fidx;
                fconsts.push_back(fval);
            }
            if (fidx > 0xffff) {
                return fail("nextvm lowering fconst index out of range");
            }
            Op fop;
            switch (fn.code[i + 2].op) {
            case MirOp::Add: fop = Op::LoadLocalAddFConst; break;
            case MirOp::Sub: fop = Op::LoadLocalSubFConst; break;
            case MirOp::Mul: fop = Op::LoadLocalMulFConst; break;
            case MirOp::Div: fop = Op::LoadLocalDivFConst; break;
            default:         fop = Op::LoadLocalAddFConst; break;
            }
            emit_u8(static_cast<std::uint8_t>(fop));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fidx));
            for (int s = 0; s < 2; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        // Class field expression from local object: push locals[obj]->field.
        if (i + 1 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::LoadClassField &&
            !jump_targets.count(i + 1) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 1].a >= 0 && fn.code[i + 1].a <= 0xffff) {
            emit_u8(static_cast<std::uint8_t>(Op::LoadLocalClassField));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fn.code[i + 1].a));
            ++i;
            pc_map[i] = static_cast<int>(out.size());
            if (mir_pc_to_byte) {
                mir_pc_to_byte->push_back(static_cast<int>(out.size()));
            }
            continue;
        }
        // Float local-dup-const expression: locals[local], locals[local] + fconst on stack.
        if (i + 3 < n &&
            mi.op == MirOp::LoadLocal &&
            fn.code[i + 1].op == MirOp::Dup &&
            fn.code[i + 2].op == MirOp::LoadFConst &&
            (fn.code[i + 3].op == MirOp::Add || fn.code[i + 3].op == MirOp::Sub ||
             fn.code[i + 3].op == MirOp::Mul || fn.code[i + 3].op == MirOp::Div) &&
            !jump_targets.count(i + 1) &&
            !jump_targets.count(i + 2) &&
            !jump_targets.count(i + 3) &&
            mi.a >= 0 && mi.a <= 0xffff &&
            fn.code[i + 2].a >= 0 &&
            fn.code[i + 2].a < static_cast<int>(fn.fconsts.size())) {
            double fval = fn.fconsts[fn.code[i + 2].a];
            int fidx = 0;
            auto fit = fconst_index.find(fval);
            if (fit != fconst_index.end()) {
                fidx = fit->second;
            } else {
                fidx = static_cast<int>(fconsts.size());
                fconst_index[fval] = fidx;
                fconsts.push_back(fval);
            }
            if (fidx > 0xffff) {
                return fail("nextvm lowering fconst index out of range");
            }
            Op fop;
            switch (fn.code[i + 3].op) {
            case MirOp::Add: fop = Op::LoadLocalDupAddFConst; break;
            case MirOp::Sub: fop = Op::LoadLocalDupSubFConst; break;
            case MirOp::Mul: fop = Op::LoadLocalDupMulFConst; break;
            case MirOp::Div: fop = Op::LoadLocalDupDivFConst; break;
            default:         fop = Op::LoadLocalDupAddFConst; break;
            }
            emit_u8(static_cast<std::uint8_t>(fop));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u16(static_cast<std::uint16_t>(fidx));
            for (int s = 0; s < 3; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            continue;
        }
        switch (mi.op) {
        case MirOp::LoadConst: {
            std::int64_t literal = 0;
            if (mi.a >= 0 && mi.a < static_cast<int>(fn.iconsts.size())) {
                literal = fn.iconsts[mi.a];
            }
            emit_load_iconst(literal);
            break;
        }
        case MirOp::LoadFConst: {
            double fval = 0.0;
            if (mi.a >= 0 && mi.a < static_cast<int>(fn.fconsts.size())) {
                fval = fn.fconsts[mi.a];
            }
            int idx = 0;
            auto it = fconst_index.find(fval);
            if (it != fconst_index.end()) {
                idx = it->second;
            } else {
                idx = static_cast<int>(fconsts.size());
                fconst_index[fval] = idx;
                fconsts.push_back(fval);
            }
            emit_u8(static_cast<std::uint8_t>(Op::LoadFConst));
            emit_u16(static_cast<std::uint16_t>(idx));
            break;
        }
        case MirOp::LoadSConst: {
            std::string sval;
            if (mi.a >= 0 && mi.a < static_cast<int>(fn.sconsts.size())) {
                sval = fn.sconsts[mi.a];
            }
            int idx = 0;
            auto it = sconst_index.find(sval);
            if (it != sconst_index.end()) {
                idx = it->second;
            } else {
                idx = static_cast<int>(sconsts.size());
                sconst_index[sval] = idx;
                sconsts.push_back(sval);
            }
            emit_u8(static_cast<std::uint8_t>(Op::LoadSConst));
            emit_u16(static_cast<std::uint16_t>(idx));
            break;
        }
        case MirOp::LoadFunction:
            emit_u8(static_cast<std::uint8_t>(Op::LoadFunc));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadLocal:
            emit_u8(static_cast<std::uint8_t>(Op::LoadLocal));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreLocal:
            emit_u8(static_cast<std::uint8_t>(Op::StoreLocal));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::IncLocal:
            emit_u8(static_cast<std::uint8_t>(Op::IncLocal));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::DecLocal:
            emit_u8(static_cast<std::uint8_t>(Op::DecLocal));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadUpvalue:
            emit_u8(static_cast<std::uint8_t>(Op::LoadUpvalue));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreUpvalue:
            emit_u8(static_cast<std::uint8_t>(Op::StoreUpvalue));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadGlobal:
            emit_u8(static_cast<std::uint8_t>(Op::LoadGlobal));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreGlobal:
            emit_u8(static_cast<std::uint8_t>(Op::StoreGlobal));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::Add:
            emit_u8(static_cast<std::uint8_t>(Op::Add));
            break;
        case MirOp::Sub:
            emit_u8(static_cast<std::uint8_t>(Op::Sub));
            break;
        case MirOp::Mul:
            emit_u8(static_cast<std::uint8_t>(Op::Mul));
            break;
        case MirOp::Div:
            emit_u8(static_cast<std::uint8_t>(Op::Div));
            break;
        case MirOp::Mod:
            emit_u8(static_cast<std::uint8_t>(Op::Mod));
            break;
        case MirOp::Shl:
            emit_u8(static_cast<std::uint8_t>(Op::Shl));
            break;
        case MirOp::Shr:
            emit_u8(static_cast<std::uint8_t>(Op::Shr));
            break;
        case MirOp::BitAnd:
            emit_u8(static_cast<std::uint8_t>(Op::BitAnd));
            break;
        case MirOp::BitOr:
            emit_u8(static_cast<std::uint8_t>(Op::BitOr));
            break;
        case MirOp::BitXor:
            emit_u8(static_cast<std::uint8_t>(Op::BitXor));
            break;
        case MirOp::BitNot:
            emit_u8(static_cast<std::uint8_t>(Op::BitNot));
            break;
        case MirOp::Neg:
            emit_u8(static_cast<std::uint8_t>(Op::Neg));
            break;
        case MirOp::Inc:
            emit_u8(static_cast<std::uint8_t>(Op::Inc));
            break;
        case MirOp::Dec:
            emit_u8(static_cast<std::uint8_t>(Op::Dec));
            break;
        case MirOp::LogicAnd:
            emit_u8(static_cast<std::uint8_t>(Op::LogicAnd));
            break;
        case MirOp::LogicOr:
            emit_u8(static_cast<std::uint8_t>(Op::LogicOr));
            break;
        case MirOp::LogicNot:
            emit_u8(static_cast<std::uint8_t>(Op::LogicNot));
            break;
        case MirOp::Eq:
            emit_u8(static_cast<std::uint8_t>(Op::Eq));
            break;
        case MirOp::Neq:
            emit_u8(static_cast<std::uint8_t>(Op::Neq));
            break;
        case MirOp::Lt:
            emit_u8(static_cast<std::uint8_t>(Op::Lt));
            break;
        case MirOp::Lte:
            emit_u8(static_cast<std::uint8_t>(Op::Lte));
            break;
        case MirOp::Gt:
            emit_u8(static_cast<std::uint8_t>(Op::Gt));
            break;
        case MirOp::Gte:
            emit_u8(static_cast<std::uint8_t>(Op::Gte));
            break;
        case MirOp::Index:
            if (mi.a != 0) {
                emit_u8(static_cast<std::uint8_t>(Op::SubArr));
            } else {
                emit_u8(static_cast<std::uint8_t>(Op::Index));
            }
            break;
        case MirOp::StoreIndex:
            emit_u8(static_cast<std::uint8_t>(Op::StoreIndex));
            break;
        case MirOp::NewArray:
            if (mi.a < 0 || mi.a > 0xffff) {
                return fail("nextvm lowering array size out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::NewArray));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::NewMapping:
            if (mi.a < 0 || mi.a > 0xffff) {
                return fail("nextvm lowering mapping size out of range");
            }
            emit_u8(static_cast<std::uint8_t>(Op::NewMapping));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::NewClass:
            emit_u8(static_cast<std::uint8_t>(Op::NewClass));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadClassField:
            emit_u8(static_cast<std::uint8_t>(Op::LoadClassField));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreClassField:
            emit_u8(static_cast<std::uint8_t>(Op::SetClassField));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::Call: {
            if (mi.a < 0) {
                return fail("nextvm lowering call with negative argc");
            }
            if (out.size() >= 3 &&
                out[out.size() - 3] == static_cast<std::uint8_t>(Op::LoadFunc)) {
                std::uint16_t fid = static_cast<std::uint16_t>(out[out.size() - 2]) |
                    static_cast<std::uint16_t>(out[out.size() - 1] << 8);
                out.erase(out.end() - 3, out.end());
                emit_u8(static_cast<std::uint8_t>(Op::CallDirect));
                emit_u16(fid);
                emit_u16(static_cast<std::uint16_t>(mi.a));
                break;
            }
            if (out.size() >= 3 &&
                out[out.size() - 3] == static_cast<std::uint8_t>(Op::LoadIConst)) {
                std::uint16_t cidx = static_cast<std::uint16_t>(out[out.size() - 2]) |
                    static_cast<std::uint16_t>(out[out.size() - 1] << 8);
                if (cidx < iconsts.size() && iconsts[cidx] >= 0 && iconsts[cidx] <= 0xffff) {
                    std::uint16_t fid = static_cast<std::uint16_t>(iconsts[cidx]);
                    out.erase(out.end() - 3, out.end());
                    emit_u8(static_cast<std::uint8_t>(Op::CallDirect));
                    emit_u16(fid);
                    emit_u16(static_cast<std::uint16_t>(mi.a));
                    break;
                }
            }
            emit_u8(static_cast<std::uint8_t>(Op::CallValue));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            break;
        }
        case MirOp::CallEfun: {
            emit_u8(static_cast<std::uint8_t>(Op::CallIntrinsic));
            emit_u16(static_cast<std::uint16_t>(mi.a));
            emit_u8(static_cast<std::uint8_t>(mi.b));
            break;
        }
        case MirOp::ForeachInit:
            emit_u8(static_cast<std::uint8_t>(Op::ForeachStep1));
            break;
        case MirOp::ForeachNext: {
            int sz = mi.a;
            std::uint16_t slot0 = 0, slot1 = 0;
            int skip = 0;
            if (i + 1 < n && fn.code[i + 1].op == MirOp::StoreLocal) {
                slot0 = static_cast<std::uint16_t>(fn.code[i + 1].a);
                skip++;
            }
            if (sz == 2 && i + 1 + skip < n && fn.code[i + 1 + skip].op == MirOp::StoreLocal) {
                slot1 = static_cast<std::uint16_t>(fn.code[i + 1 + skip].a);
                skip++;
            }
            emit_u8(static_cast<std::uint8_t>(Op::ForeachStep2));
            emit_u8(static_cast<std::uint8_t>(sz));
            emit_u16(slot0);
            emit_u16(slot1);
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, mi.b});
            for (int s = 0; s < skip; ++s) {
                ++i;
                pc_map[i] = static_cast<int>(out.size());
                if (mir_pc_to_byte) {
                    mir_pc_to_byte->push_back(static_cast<int>(out.size()));
                }
            }
            break;
        }
        case MirOp::JumpIfFalse: {
            emit_u8(static_cast<std::uint8_t>(Op::JumpIfFalse));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::JumpIfTrue: {
            emit_u8(static_cast<std::uint8_t>(Op::JumpIfTrue));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::Jump: {
            emit_u8(static_cast<std::uint8_t>(Op::Jump));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::Catch: {
            emit_u8(static_cast<std::uint8_t>(Op::Catch));
            int patch_pos = static_cast<int>(out.size());
            emit_u16(0);
            patches.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::Return:
            emit_u8(static_cast<std::uint8_t>(Op::Return));
            break;
        case MirOp::Pop:
            emit_u8(static_cast<std::uint8_t>(Op::Pop));
            break;
        case MirOp::Dup:
            emit_u8(static_cast<std::uint8_t>(Op::Dup));
            break;
        case MirOp::Upset:
            emit_u8(static_cast<std::uint8_t>(Op::Upset));
            emit_u8(static_cast<std::uint8_t>(mi.a == 1 ? 21 : 22));
            emit_u8(static_cast<std::uint8_t>(mi.b));
            break;
        default:
            return fail("unsupported MIR opcode in nextvm lowering");
        }
    }

    pc_map[n] = static_cast<int>(out.size());
    for (const auto &p : patches) {
        if (!pc_map.count(p.second)) {
            return fail("invalid nextvm jump target");
        }
        int target = pc_map[p.second];
        int base = p.first + 2;
        int rel = target - base;
        if (rel < -32768 || rel > 32767) {
            return fail("nextvm jump target too far");
        }
        std::uint16_t bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(rel));
        out[p.first + 0] = static_cast<std::uint8_t>(bits & 0xff);
        out[p.first + 1] = static_cast<std::uint8_t>((bits >> 8) & 0xff);
    }
    return true;
}


struct SectionWriter {
    std::vector<std::uint8_t> payload;

    void WriteU8(std::uint8_t v) {
        payload.push_back(v);
    }

    void WriteU16(std::uint16_t v) {
        payload.push_back(static_cast<std::uint8_t>(v & 0xff));
        payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    }

    void WriteU32(std::uint32_t v) {
        payload.push_back(static_cast<std::uint8_t>(v & 0xff));
        payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        payload.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
        payload.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    }

    void WriteU64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            payload.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
        }
    }

    void WriteString(const std::string &s) {
        WriteU32(static_cast<std::uint32_t>(s.size()));
        for (char c : s) {
            payload.push_back(static_cast<std::uint8_t>(c));
        }
    }

    void WriteBytes(const std::uint8_t *data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            payload.push_back(data[i]);
        }
    }
};

static void WriteClassFieldDefault(SectionWriter &sw, const ClassFieldDefault &def) {
    sw.WriteU8(static_cast<std::uint8_t>(def.kind));
    switch (def.kind) {
    case ClassFieldDefault::Kind::Int:
        sw.WriteU64(static_cast<std::uint64_t>(def.int_value));
        break;
    case ClassFieldDefault::Kind::Float: {
        std::uint64_t bits = 0;
        static_assert(sizeof(double) == sizeof(std::uint64_t), "double must be 64 bits");
        std::memcpy(&bits, &def.float_value, sizeof(double));
        sw.WriteU64(bits);
        break;
    }
    case ClassFieldDefault::Kind::String:
        sw.WriteString(def.string_value);
        break;
    case ClassFieldDefault::Kind::Mapping:
        sw.WriteU32(static_cast<std::uint32_t>(def.mapping_pairs.size()));
        for (const auto &pair : def.mapping_pairs) {
            WriteClassFieldDefault(sw, pair.first);
            WriteClassFieldDefault(sw, pair.second);
        }
        break;
    case ClassFieldDefault::Kind::Array:
        sw.WriteU32(static_cast<std::uint32_t>(def.array_items.size()));
        for (const auto &item : def.array_items) {
            WriteClassFieldDefault(sw, item);
        }
        break;
    case ClassFieldDefault::Kind::Zero:
    default:
        break;
    }
}

static void EmitSection(std::vector<std::uint8_t> &out, std::uint8_t id, const SectionWriter &sw) {
    out.push_back(id);
    std::uint32_t sz = static_cast<std::uint32_t>(sw.payload.size());
    out.push_back(static_cast<std::uint8_t>(sz & 0xff));
    out.push_back(static_cast<std::uint8_t>((sz >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((sz >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((sz >> 24) & 0xff));
    out.insert(out.end(), sw.payload.begin(), sw.payload.end());
}

bool WriteMirAsNextVmBytecode(const MirModule &module, const std::string &module_name, const std::string &out_path, std::string *error, const std::vector<SourceMapEntry> &source_map) {
    struct LoweredFunction {
        std::string name;
        int nargs = 0;
        int nlocals = 0;
        int max_stack = 0;
        int from = 0;
        int to = 0;
        std::vector<std::pair<int, int>> upvalues;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> line_map;
        const MirFunction *mir_fn = nullptr;
    };

    bool has_init = !module.init_function.code.empty() &&
                    !(module.init_function.code.size() == 1 &&
                      module.init_function.code[0].op == MirOp::Return);

    std::unordered_map<std::int64_t, int> iconst_index;
    std::vector<std::int64_t> iconsts;
    if (has_init) {
        for (std::int64_t v : module.init_function.iconsts) {
            if (!iconst_index.count(v)) {
                iconst_index[v] = static_cast<int>(iconsts.size());
                iconsts.push_back(v);
            }
        }
    }
    for (const auto &fn : module.functions) {
        for (std::int64_t v : fn.iconsts) {
            if (!iconst_index.count(v)) {
                iconst_index[v] = static_cast<int>(iconsts.size());
                iconsts.push_back(v);
            }
        }
    }

    std::unordered_map<double, int> fconst_index;
    std::vector<double> fconsts;
    if (has_init) {
        for (double v : module.init_function.fconsts) {
            if (!fconst_index.count(v)) {
                fconst_index[v] = static_cast<int>(fconsts.size());
                fconsts.push_back(v);
            }
        }
    }

    std::unordered_map<std::string, int> sconst_index;
    std::vector<std::string> sconsts;
    if (has_init) {
        for (const auto &s : module.init_function.sconsts) {
            if (!sconst_index.count(s)) {
                sconst_index[s] = static_cast<int>(sconsts.size());
                sconsts.push_back(s);
            }
        }
    }

    std::vector<LoweredFunction> lowered;
    std::vector<std::uint8_t> code;
    lowered.reserve(module.functions.size() + (has_init ? 1 : 0));

    if (has_init) {
        LoweredFunction lf;
        lf.name = "__init_globals";
        lf.nargs = 0;
        lf.nlocals = static_cast<int>(module.init_function.locals.size());
        lf.max_stack = module.init_function.max_stack;
        lf.from = static_cast<int>(code.size());
        lf.mir_fn = &module.init_function;

        std::vector<int> mir_pc_to_byte;
        if (!LowerMirToNextVm(module.init_function, iconst_index, iconsts, fconst_index, fconsts, sconst_index, sconsts, code, error, &mir_pc_to_byte)) {
            if (error && !error->empty()) {
                *error = "write nextvm bytecode failed for __init_globals: " + *error;
            }
            return false;
        }
        lf.to = static_cast<int>(code.size());

        int prev_line = -1;
        for (int i = 0; i < static_cast<int>(module.init_function.code.size()) && i < static_cast<int>(mir_pc_to_byte.size()); ++i) {
            int line = module.init_function.code[i].line;
            if (line <= 0 || line == prev_line) {
                continue;
            }
            prev_line = line;
            lf.line_map.push_back({
                static_cast<std::uint32_t>(line),
                static_cast<std::uint32_t>(mir_pc_to_byte[i]),
            });
        }
        lowered.push_back(std::move(lf));
    }

    for (const auto &fn : module.functions) {
        LoweredFunction lf;
        lf.name = fn.name;
        lf.nargs = fn.nargs;
        lf.nlocals = static_cast<int>(fn.locals.size()) + fn.nargs;
        lf.max_stack = fn.max_stack;
        lf.from = static_cast<int>(code.size());
        lf.mir_fn = &fn;

        for (int ui = 0; ui < static_cast<int>(fn.upvalue_source_kind.size()); ++ui) {
            lf.upvalues.push_back({fn.upvalue_source_kind[ui], fn.upvalue_source_index[ui]});
        }

        std::vector<int> mir_pc_to_byte;
        if (!LowerMirToNextVm(fn, iconst_index, iconsts, fconst_index, fconsts, sconst_index, sconsts, code, error, &mir_pc_to_byte)) {
            if (error && !error->empty()) {
                *error = "write nextvm bytecode failed for function " + fn.name + ": " + *error;
            }
            return false;
        }
        lf.to = static_cast<int>(code.size());

        int prev_line = -1;
        for (int i = 0; i < static_cast<int>(fn.code.size()) && i < static_cast<int>(mir_pc_to_byte.size()); ++i) {
            int line = fn.code[i].line;
            if (line <= 0 || line == prev_line) {
                continue;
            }
            prev_line = line;
            lf.line_map.push_back({
                static_cast<std::uint32_t>(line),
                static_cast<std::uint32_t>(mir_pc_to_byte[i]),
            });
        }
        lowered.push_back(std::move(lf));
    }

    std::vector<std::string> class_names;
    if (!module.class_order.empty()) {
        class_names = module.class_order;
    } else {
        for (const auto &it : module.class_fields) {
            class_names.push_back(it.first);
        }
    }

    std::vector<std::uint8_t> init_bytes;
    if (has_init && !lowered.empty() && lowered[0].name == "__init_globals") {
        init_bytes.assign(code.begin() + lowered[0].from, code.begin() + lowered[0].to);
    }

    std::uint32_t flags = 0;
    if (!init_bytes.empty()) flags |= 0x01;
    bool has_debug = !module.global_variables.empty();
    for (const auto &fn : module.functions) {
        if (!fn.locals.empty() || !fn.upvalues.empty()) {
            has_debug = true;
            break;
        }
    }
    if (has_debug) flags |= 0x02;

    std::vector<std::uint8_t> all;

    // Preamble: "LPC\0" + version u32
    all.push_back('L');
    all.push_back('P');
    all.push_back('C');
    all.push_back('\0');
    WriteU32(all, 2);

    // Section 0x00 - HEADER
    {
        SectionWriter sw;
        sw.WriteString(module_name);
        sw.WriteU32(flags);
        EmitSection(all, 0x00, sw);
    }

    // Section 0x01 - CONSTANTS
    {
        SectionWriter sw;
        sw.WriteU32(static_cast<std::uint32_t>(iconsts.size()));
        for (std::int64_t v : iconsts) {
            sw.WriteU64(static_cast<std::uint64_t>(v));
        }
        sw.WriteU32(static_cast<std::uint32_t>(fconsts.size()));
        for (double v : fconsts) {
            std::uint64_t bits = 0;
            static_assert(sizeof(double) == sizeof(std::uint64_t), "double must be 64 bits");
            std::memcpy(&bits, &v, sizeof(double));
            sw.WriteU64(bits);
        }
        sw.WriteU32(static_cast<std::uint32_t>(sconsts.size()));
        for (const auto &s : sconsts) {
            sw.WriteString(s);
        }
        EmitSection(all, 0x01, sw);
    }

    // Section 0x02 - CLASSES
    {
        SectionWriter sw;
        sw.WriteU32(static_cast<std::uint32_t>(class_names.size()));
        for (std::size_t ci = 0; ci < class_names.size(); ++ci) {
            const auto &name = class_names[ci];
            auto it = module.class_fields.find(name);
            std::uint16_t nfields = it == module.class_fields.end()
                ? 0
                : static_cast<std::uint16_t>(it->second.size());
            sw.WriteString(name);
            sw.WriteU16(nfields);
            std::uint16_t parent_idx = 0xFFFF;
            auto pit = module.class_parent.find(name);
            if (pit != module.class_parent.end()) {
                for (std::size_t j = 0; j < class_names.size(); ++j) {
                    if (class_names[j] == pit->second) {
                        parent_idx = static_cast<std::uint16_t>(j);
                        break;
                    }
                }
            }
            sw.WriteU16(parent_idx);
            if (it != module.class_fields.end()) {
                const auto defaults_it = module.class_field_defaults.find(name);
                const std::vector<ClassFieldDefault> *defaults =
                    defaults_it == module.class_field_defaults.end() ? nullptr : &defaults_it->second;
                for (std::size_t fi = 0; fi < it->second.size(); ++fi) {
                    const auto &fname = it->second[fi];
                    sw.WriteString(fname);
                    ClassFieldDefault def;
                    if (defaults && fi < defaults->size()) {
                        def = (*defaults)[fi];
                    }
                    WriteClassFieldDefault(sw, def);
                }
            }
        }
        EmitSection(all, 0x02, sw);
    }

    // Section 0x03 - FUNCTIONS
    {
        SectionWriter sw;
        sw.WriteU32(static_cast<std::uint32_t>(lowered.size()));
        for (const auto &lf : lowered) {
            sw.WriteString(lf.name);
            sw.WriteU16(static_cast<std::uint16_t>(lf.nargs));
            sw.WriteU16(static_cast<std::uint16_t>(lf.nlocals));
            sw.WriteU16(static_cast<std::uint16_t>(lf.max_stack));
            sw.WriteU32(static_cast<std::uint32_t>(lf.from));
            sw.WriteU32(static_cast<std::uint32_t>(lf.to));
            sw.WriteU16(static_cast<std::uint16_t>(lf.upvalues.size()));
            for (const auto &uv : lf.upvalues) {
                sw.WriteU16(static_cast<std::uint16_t>(uv.first));
                sw.WriteU16(static_cast<std::uint16_t>(uv.second < 0 ? 0xffff : uv.second));
            }
        }
        EmitSection(all, 0x03, sw);
    }

    // Section 0x04 - LINE_TABLE
    {
        SectionWriter sw;
        std::uint32_t total_lines = 0;
        for (const auto &lf : lowered) {
            total_lines += static_cast<std::uint32_t>(lf.line_map.size());
        }
        sw.WriteU32(total_lines);
        for (const auto &lf : lowered) {
            for (const auto &it : lf.line_map) {
                sw.WriteU32(it.first);
                sw.WriteU32(it.second);
            }
        }
        EmitSection(all, 0x04, sw);
    }

    // Section 0x05 - CODE
    {
        SectionWriter sw;
        sw.WriteU32(static_cast<std::uint32_t>(code.size()));
        sw.WriteBytes(code.data(), code.size());
        sw.WriteU32(static_cast<std::uint32_t>(init_bytes.size()));
        if (!init_bytes.empty()) {
            sw.WriteBytes(init_bytes.data(), init_bytes.size());
        }
        EmitSection(all, 0x05, sw);
    }

    // Section 0x06 - DEBUG
    {
        SectionWriter sw;
        sw.WriteString(module_name);
        sw.WriteU32(static_cast<std::uint32_t>(module.global_variables.size()));
        for (const auto &gv : module.global_variables) {
            sw.WriteString(gv);
        }
        sw.WriteU32(static_cast<std::uint32_t>(lowered.size()));
        for (const auto &lf : lowered) {
            if (lf.mir_fn) {
                auto &fn = *lf.mir_fn;
                sw.WriteU16(static_cast<std::uint16_t>(fn.nargs));
                for (int i = 0; i < fn.nargs; ++i) {
                    if (i < static_cast<int>(fn.locals.size())) {
                        sw.WriteString(fn.locals[i]);
                    } else {
                        sw.WriteString("");
                    }
                }
                int n_local_names = static_cast<int>(fn.locals.size()) - fn.nargs;
                sw.WriteU16(static_cast<std::uint16_t>(n_local_names > 0 ? n_local_names : 0));
                for (int i = fn.nargs; i < static_cast<int>(fn.locals.size()); ++i) {
                    sw.WriteString(fn.locals[i]);
                }
                sw.WriteU16(static_cast<std::uint16_t>(fn.upvalues.size()));
                for (const auto &uv : fn.upvalues) {
                    sw.WriteString(uv);
                }
            } else {
                sw.WriteU16(0);
                sw.WriteU16(0);
                sw.WriteU16(0);
            }
        }
        sw.WriteU32(static_cast<std::uint32_t>(source_map.size()));
        for (const auto &sm : source_map) {
            sw.WriteU32(static_cast<std::uint32_t>(sm.output_line));
            sw.WriteU32(static_cast<std::uint32_t>(sm.source_line));
            sw.WriteString(sm.source_path);
        }
        EmitSection(all, 0x06, sw);
    }

    // Section 0x07 - LIFECYCLE
    {
        std::uint16_t create_idx = 0xFFFF;
        std::uint16_t on_loadin_idx = 0xFFFF;
        std::uint16_t on_destruct_idx = 0xFFFF;
        for (std::size_t i = 0; i < lowered.size(); ++i) {
            if (lowered[i].name == "create") create_idx = static_cast<std::uint16_t>(i);
            else if (lowered[i].name == "on_loadin") on_loadin_idx = static_cast<std::uint16_t>(i);
            else if (lowered[i].name == "on_destruct") on_destruct_idx = static_cast<std::uint16_t>(i);
        }
        if (create_idx != 0xFFFF || on_loadin_idx != 0xFFFF || on_destruct_idx != 0xFFFF) {
            SectionWriter sw;
            sw.WriteU16(create_idx);
            sw.WriteU16(on_loadin_idx);
            sw.WriteU16(on_destruct_idx);
            EmitSection(all, 0x07, sw);
        }
    }

    std::ofstream out(out_path.c_str(), std::ios::binary);
    if (!out.is_open()) {
        if (error) {
            *error = "failed to open output bytecode file";
        }
        return false;
    }
    out.write(reinterpret_cast<const char *>(all.data()), static_cast<std::streamsize>(all.size()));
    out.close();
    return true;
}

bool WriteMirModuleSetAsNextVmBytecodeAtPath(
    const MirModule &module,
    const std::string &source_path,
    const std::string &workspace_root,
    const std::string &out_root,
    std::string *error,
    std::string *out_module_name,
    const std::vector<SourceMapEntry> &source_map) {
    namespace fs = std::filesystem;

    fs::path src = fs::weakly_canonical(fs::path(source_path));
    fs::path ws = fs::weakly_canonical(fs::path(workspace_root));
    fs::path bin = fs::path(out_root);

    if (!fs::exists(src)) {
        if (error) {
            *error = "source path does not exist";
        }
        return false;
    }

    fs::path rel;
    try {
        rel = fs::relative(src.parent_path(), ws);
    } catch (...) {
        rel.clear();
    }

    fs::path out_dir = bin;
    if (!rel.empty() && rel != fs::path(".")) {
        out_dir /= rel;
    }

    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        if (error) {
            *error = "failed to create output directory";
        }
        return false;
    }

    fs::path rel_file;
    try {
        rel_file = fs::relative(src, ws);
    } catch (...) {
        rel_file = src.filename();
    }
    std::string module_name = rel_file.replace_extension("").generic_string();
    const fs::path out_file = out_dir / (src.stem().string() + ".nb");
    if (!WriteMirAsNextVmBytecode(module, module_name, out_file.string(), error, source_map)) {
        return false;
    }

    if (out_module_name) {
        *out_module_name = module_name;
    }
    return true;
}

} // namespace frontend
} // namespace lpc
