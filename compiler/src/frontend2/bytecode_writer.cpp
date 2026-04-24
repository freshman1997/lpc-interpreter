#include "frontend2/bytecode_writer.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace lpc {
namespace frontend2 {

enum class V1Op : std::uint8_t {
    op_load_global = 0,
    op_load_local = 1,
    op_store_global = 2,
    op_store_local = 3,
    op_load_func = 4,
    op_load_iconst = 5,
    op_load_fconst = 6,
    op_load_sconst = 7,
    op_load_0 = 8,
    op_load_1 = 9,
    op_add = 10,
    op_sub = 11,
    op_mul = 12,
    op_div = 13,
    op_mod = 14,
    op_binary_lm = 15,
    op_binary_rm = 16,
    op_binary_and = 17,
    op_binary_or = 18,
    op_binary_not = 19,
    op_binary_xor = 20,
    op_inc = 21,
    op_dec = 22,
    op_minus = 23,
    op_cmp_and = 24,
    op_cmp_or = 25,
    op_cmp_not = 26,
    op_cmp_eq = 27,
    op_cmp_neq = 28,
    op_cmp_gt = 29,
    op_cmp_gte = 30,
    op_cmp_lt = 31,
    op_cmp_lte = 32,
    op_or = 33,
    op_test = 34,
    op_index = 35,
    op_new_array = 36,
    op_sub_arr = 37,
    op_new_mapping = 38,
    op_upset = 39,
    op_call = 40,
    op_call_virtual = 41,
    op_return = 42,
    op_set_upvalue = 43,
    op_get_upvalue = 44,
    op_new_class = 45,
    op_set_class_field = 46,
    op_load_class_field = 47,
    op_goto = 48,
    op_switch = 49,
    op_foreach_step1 = 50,
    op_foreach_step2 = 51,
    op_pop = 52,
    op_test_not = 53,
    op_store_index = 54,
    op_dup = 55,
    op_catch = 56,
};

static void WriteU8(std::vector<char> &buf, std::uint8_t v) {
    buf.push_back(static_cast<char>(v));
}

static void WriteU16(std::vector<char> &buf, std::uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xff));
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
}

static void WriteU32(std::vector<char> &buf, std::uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xff));
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
    buf.push_back(static_cast<char>((v >> 16) & 0xff));
    buf.push_back(static_cast<char>((v >> 24) & 0xff));
}

static bool LowerMir(
    const MirFunction &fn,
    std::unordered_map<int, int> &iconst_index,
    std::unordered_map<float, int> &fconst_index,
    std::unordered_map<std::string, int> &sconst_index,
    std::vector<int> &iconsts,
    std::vector<float> &fconsts,
    std::vector<std::string> &sconsts,
    std::vector<char> &out,
    std::string *err,
    std::vector<int> *mir_pc_to_byte) {
    std::unordered_map<int, int> pc_map;
    std::vector<std::pair<int, int>> patch_test;
    std::vector<std::pair<int, int>> patch_goto;

    for (int i = 0; i < static_cast<int>(fn.code.size()); ++i) {
        pc_map[i] = static_cast<int>(out.size());
        if (mir_pc_to_byte) {
            mir_pc_to_byte->push_back(static_cast<int>(out.size()));
        }
        const MirInstr &mi = fn.code[i];
        switch (mi.op) {
        case MirOp::LoadConst: {
            int literal = 0;
            if (mi.a >= 0 && mi.a < static_cast<int>(fn.iconsts.size())) {
                literal = fn.iconsts[mi.a];
            }
            int idx = 0;
            auto it = iconst_index.find(literal);
            if (it != iconst_index.end()) {
                idx = it->second;
            } else {
                idx = static_cast<int>(iconsts.size());
                iconst_index[literal] = idx;
                iconsts.push_back(literal);
            }
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_iconst));
            WriteU16(out, static_cast<std::uint16_t>(idx));
            break;
        }
        case MirOp::LoadFConst: {
            float fval = 0.0f;
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
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_fconst));
            WriteU16(out, static_cast<std::uint16_t>(idx));
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
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_sconst));
            WriteU16(out, static_cast<std::uint16_t>(idx));
            break;
        }
        case MirOp::LoadFunction:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_func));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadLocal:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_local));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreLocal:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_store_local));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadUpvalue:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_get_upvalue));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreUpvalue:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_set_upvalue));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadGlobal:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_global));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreGlobal:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_store_global));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::Add:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_add));
            break;
        case MirOp::Sub:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_sub));
            break;
        case MirOp::Mul:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_mul));
            break;
        case MirOp::Div:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_div));
            break;
        case MirOp::Mod:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_mod));
            break;
        case MirOp::Shl:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_binary_lm));
            break;
        case MirOp::Shr:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_binary_rm));
            break;
        case MirOp::BitAnd:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_binary_and));
            break;
        case MirOp::BitOr:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_binary_or));
            break;
        case MirOp::BitNot:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_binary_not));
            break;
        case MirOp::BitXor:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_binary_xor));
            break;
        case MirOp::Neg:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_minus));
            break;
        case MirOp::LogicAnd:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_and));
            break;
        case MirOp::LogicOr:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_or));
            break;
        case MirOp::LogicNot:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_not));
            break;
        case MirOp::Eq:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_eq));
            break;
        case MirOp::Neq:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_neq));
            break;
        case MirOp::Lt:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_lt));
            break;
        case MirOp::Lte:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_lte));
            break;
        case MirOp::Gt:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_gt));
            break;
        case MirOp::Gte:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_cmp_gte));
            break;
        case MirOp::Index:
            if (mi.a != 0) {
                WriteU8(out, static_cast<std::uint8_t>(V1Op::op_sub_arr));
            } else {
                WriteU8(out, static_cast<std::uint8_t>(V1Op::op_index));
            }
            break;
        case MirOp::StoreIndex:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_store_index));
            break;
        case MirOp::NewArray:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_new_array));
            WriteU32(out, static_cast<std::uint32_t>(mi.a));
            break;
        case MirOp::NewClass:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_new_class));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::LoadClassField:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_load_class_field));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::StoreClassField:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_set_class_field));
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            break;
        case MirOp::Call:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_call));
            WriteU8(out, 0);
            break;
        case MirOp::CallEfun:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_call));
            WriteU8(out, 1);
            WriteU16(out, static_cast<std::uint16_t>(mi.a));
            WriteU8(out, static_cast<std::uint8_t>(mi.b));
            break;
        case MirOp::ForeachInit:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_foreach_step1));
            break;
        case MirOp::ForeachNext:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_foreach_step2));
            WriteU8(out, static_cast<std::uint8_t>(mi.a));
            {
                int patch_pos = static_cast<int>(out.size());
                WriteU32(out, 0);
                patch_goto.push_back({patch_pos, mi.b});
            }
            break;
        case MirOp::Switch:
            if (err) {
                *err = "MirOp::Switch lowering is not implemented";
            }
            return false;
        case MirOp::JumpIfFalse: {
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_test));
            int patch_pos = static_cast<int>(out.size());
            WriteU32(out, 0);
            patch_test.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::JumpIfTrue: {
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_test_not));
            int patch_pos = static_cast<int>(out.size());
            WriteU32(out, 0);
            patch_test.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::Jump: {
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_goto));
            int patch_pos = static_cast<int>(out.size());
            WriteU32(out, 0);
            patch_goto.push_back({patch_pos, mi.a});
            break;
        }
        case MirOp::Return:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_return));
            break;
        case MirOp::Pop:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_pop));
            break;
        case MirOp::Dup:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_dup));
            break;
        case MirOp::NewMapping:
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_new_mapping));
            WriteU32(out, static_cast<std::uint32_t>(mi.a));
            break;
        case MirOp::Catch: {
            WriteU8(out, static_cast<std::uint8_t>(V1Op::op_catch));
            int patch_pos = static_cast<int>(out.size());
            WriteU32(out, 0);
            patch_goto.push_back({patch_pos, mi.a});
            break;
        }
        default:
            if (err) {
                *err = "unsupported MIR opcode in lowering";
            }
            return false;
        }
    }

    pc_map[static_cast<int>(fn.code.size())] = static_cast<int>(out.size());

    auto patch = [&](const std::vector<std::pair<int, int>> &items) {
        for (const auto &p : items) {
            if (!pc_map.count(p.second)) {
                if (err) {
                    *err = "invalid jump target";
                }
                return false;
            }
            std::uint32_t target = static_cast<std::uint32_t>(pc_map[p.second]);
            out[p.first + 0] = static_cast<char>(target & 0xff);
            out[p.first + 1] = static_cast<char>((target >> 8) & 0xff);
            out[p.first + 2] = static_cast<char>((target >> 16) & 0xff);
            out[p.first + 3] = static_cast<char>((target >> 24) & 0xff);
        }
        return true;
    };

    return patch(patch_test) && patch(patch_goto);
}

bool WriteMirAsV1Bytecode(const MirModule &module, const std::string &module_name, const std::string &out_path, std::string *error) {
    struct LoweredFunction {
        std::string name;
        int nargs = 0;
        int nlocals = 0;
        int nupvalues = 0;
        std::vector<int> upvalue_source_kind;
        std::vector<int> upvalue_source_index;
        int from = 0;
        int to = 0;
        std::vector<char> code;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> line_map;
    };

    std::unordered_map<int, int> iconst_index;
    std::vector<int> iconsts;
    for (const auto &fn : module.functions) {
        for (int v : fn.iconsts) {
            if (!iconst_index.count(v)) {
                iconst_index[v] = static_cast<int>(iconsts.size());
                iconsts.push_back(v);
            }
        }
    }
    for (int v : module.init_function.iconsts) {
        if (!iconst_index.count(v)) {
            iconst_index[v] = static_cast<int>(iconsts.size());
            iconsts.push_back(v);
        }
    }

    std::unordered_map<float, int> fconst_index;
    std::vector<float> fconsts;
    for (const auto &fn : module.functions) {
        for (float v : fn.fconsts) {
            if (!fconst_index.count(v)) {
                fconst_index[v] = static_cast<int>(fconsts.size());
                fconsts.push_back(v);
            }
        }
    }
    for (float v : module.init_function.fconsts) {
        if (!fconst_index.count(v)) {
            fconst_index[v] = static_cast<int>(fconsts.size());
            fconsts.push_back(v);
        }
    }

    std::unordered_map<std::string, int> sconst_index;
    std::vector<std::string> sconsts;
    for (const auto &fn : module.functions) {
        for (const auto &s : fn.sconsts) {
            if (!sconst_index.count(s)) {
                sconst_index[s] = static_cast<int>(sconsts.size());
                sconsts.push_back(s);
            }
        }
    }
    for (const auto &s : module.init_function.sconsts) {
        if (!sconst_index.count(s)) {
            sconst_index[s] = static_cast<int>(sconsts.size());
            sconsts.push_back(s);
        }
    }

    std::vector<LoweredFunction> lowered;
    lowered.reserve(module.functions.size());
    int cur_pc = 0;
    for (const auto &fn : module.functions) {
        LoweredFunction lf;
        lf.name = fn.name;
        lf.nargs = fn.nargs;
        lf.nlocals = static_cast<int>(fn.locals.size()) + fn.nargs;
        lf.nupvalues = static_cast<int>(fn.upvalues.size());
        lf.upvalue_source_kind = fn.upvalue_source_kind;
        lf.upvalue_source_index = fn.upvalue_source_index;
        std::vector<int> mir_pc_to_byte;
        if (!LowerMir(fn, iconst_index, fconst_index, sconst_index, iconsts, fconsts, sconsts, lf.code, error, &mir_pc_to_byte)) {
            return false;
        }
        lf.from = cur_pc;
        cur_pc += static_cast<int>(lf.code.size());
        lf.to = cur_pc;

        int prev_line = -1;
        for (int i = 0; i < static_cast<int>(fn.code.size()) && i < static_cast<int>(mir_pc_to_byte.size()); ++i) {
            int line = fn.code[i].line;
            if (line <= 0 || line == prev_line) {
                continue;
            }
            prev_line = line;
            lf.line_map.push_back({
                static_cast<std::uint32_t>(line - 1),
                static_cast<std::uint32_t>(lf.from + mir_pc_to_byte[i]),
            });
        }

        lowered.push_back(std::move(lf));
    }

    std::vector<char> func_codes;
    for (const auto &lf : lowered) {
        for (char c : lf.code) {
            func_codes.push_back(c);
        }
    }

    std::vector<char> init_codes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> init_line_map;
    bool has_init = !module.init_function.code.empty() &&
                    !(module.init_function.code.size() == 1 &&
                      module.init_function.code[0].op == MirOp::Return);
    if (has_init) {
        if (!LowerMir(module.init_function, iconst_index, fconst_index, sconst_index, iconsts, fconsts, sconsts, init_codes, error, nullptr)) {
            return false;
        }
        if (!init_codes.empty() && static_cast<std::uint8_t>(init_codes.back()) == static_cast<std::uint8_t>(V1Op::op_return)) {
            init_codes.pop_back();
        }
        int prev_line = -1;
        for (int i = 0; i < static_cast<int>(module.init_function.code.size()); ++i) {
            int line = module.init_function.code[i].line;
            if (line <= 0 || line == prev_line) continue;
            prev_line = line;
            init_line_map.push_back({
                static_cast<std::uint32_t>(line - 1),
                static_cast<std::uint32_t>(0),
            });
        }
    }

    std::vector<char> all;

    auto write_bytes = [&](const char *p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            all.push_back(p[i]);
        }
    };

    auto write_u32_file = [&](std::uint32_t v) {
        WriteU32(all, v);
    };

    write_u32_file(static_cast<std::uint32_t>(module_name.size()));
    write_bytes(module_name.data(), module_name.size());

    std::vector<std::string> class_names;
    if (!module.class_order.empty()) {
        class_names = module.class_order;
    } else {
        for (const auto &it : module.class_fields) {
            class_names.push_back(it.first);
        }
    }

    write_u32_file(static_cast<std::uint32_t>(class_names.size()));
    for (const auto &name : class_names) {
        auto it = module.class_fields.find(name);
        if (it == module.class_fields.end()) {
            write_u32_file(0);
            WriteU8(all, 0);
            WriteU16(all, 0);
            continue;
        }
        write_u32_file(static_cast<std::uint32_t>(name.size()));
        write_bytes(name.data(), name.size());
        WriteU8(all, 0);
        WriteU16(all, static_cast<std::uint16_t>(it->second.size()));
    }
    WriteU16(all, static_cast<std::uint16_t>(-1));
    WriteU16(all, static_cast<std::uint16_t>(-1));
    WriteU16(all, static_cast<std::uint16_t>(-1));

    std::vector<std::pair<std::uint32_t, std::uint32_t>> line_map;
    for (const auto &lf : lowered) {
        for (const auto &it : lf.line_map) {
            line_map.push_back(it);
        }
    }

    write_u32_file(static_cast<std::uint32_t>(line_map.size()));
    for (const auto &it : line_map) {
        WriteU32(all, it.first);
        WriteU32(all, it.second);
    }

    write_u32_file(static_cast<std::uint32_t>(lowered.size()));
    for (const auto &lf : lowered) {
        write_u32_file(static_cast<std::uint32_t>(lf.name.size()));
        write_bytes(lf.name.data(), lf.name.size());
        WriteU8(all, 3);
        WriteU8(all, 0);
        WriteU16(all, static_cast<std::uint16_t>(lf.nargs));
        WriteU16(all, static_cast<std::uint16_t>(lf.nlocals));
        WriteU16(all, static_cast<std::uint16_t>(lf.nupvalues));

        for (int ui = 0; ui < lf.nupvalues; ++ui) {
            int kind = 0;
            int index = -1;
            if (ui < static_cast<int>(lf.upvalue_source_kind.size())) {
                kind = lf.upvalue_source_kind[ui];
            }
            if (ui < static_cast<int>(lf.upvalue_source_index.size())) {
                index = lf.upvalue_source_index[ui];
            }
            WriteU16(all, static_cast<std::uint16_t>(kind));
            WriteU16(all, static_cast<std::uint16_t>(index < 0 ? 0xffff : index));
        }

        WriteU32(all, static_cast<std::uint32_t>(lf.from));
        WriteU32(all, static_cast<std::uint32_t>(lf.to));
    }

    write_u32_file(static_cast<std::uint32_t>(module.global_variables.size()));
    for (int i = 0; i < static_cast<int>(module.global_variables.size()); ++i) {
        WriteU8(all, 0);
    }

    write_u32_file(static_cast<std::uint32_t>(iconsts.size()));
    for (int v : iconsts) {
        WriteU32(all, static_cast<std::uint32_t>(v));
    }

    write_u32_file(static_cast<std::uint32_t>(fconsts.size()));
    for (float v : fconsts) {
        std::uint32_t bits = 0;
        static_assert(sizeof(float) == sizeof(std::uint32_t), "float must be 32 bits");
        std::memcpy(&bits, &v, sizeof(float));
        WriteU32(all, bits);
    }

    write_u32_file(static_cast<std::uint32_t>(sconsts.size()));
    for (const auto &s : sconsts) {
        WriteU32(all, static_cast<std::uint32_t>(s.size()));
        for (char c : s) {
            all.push_back(c);
        }
    }

    WriteU8(all, 1);

    write_u32_file(static_cast<std::uint32_t>(init_codes.size()));
    if (!init_codes.empty()) {
        for (char c : init_codes) {
            all.push_back(c);
        }
    }

    write_u32_file(static_cast<std::uint32_t>(init_line_map.size()));
    for (const auto &it : init_line_map) {
        WriteU32(all, it.first);
        WriteU32(all, it.second);
    }

    write_u32_file(static_cast<std::uint32_t>(func_codes.size()));
    for (char c : func_codes) {
        all.push_back(c);
    }
    write_u32_file(0);

    std::ofstream out(out_path.c_str(), std::ios::binary);
    if (!out.is_open()) {
        if (error) {
            *error = "failed to open output bytecode file";
        }
        return false;
    }
    out.write(all.data(), static_cast<std::streamsize>(all.size()));
    out.close();
    return true;
}

bool WriteMirModuleSetAsV1Bytecode(const MirModule &module, const std::string &out_dir, std::string *error) {
    for (const auto &fn : module.functions) {
        if (fn.name.empty()) {
            continue;
        }
        MirModule one;
        one.class_order = module.class_order;
        one.class_fields = module.class_fields;
        one.global_variables = module.global_variables;
        one.init_function = module.init_function;
        one.functions.push_back(fn);
        std::string out = out_dir + "/" + fn.name + ".b";
        if (!WriteMirAsV1Bytecode(one, fn.name, out, error)) {
            std::stringstream msg;
            msg << "write bytecode failed for function: " << fn.name;
            if (error && !error->empty()) {
                msg << ", reason=" << *error;
            }
            if (error) {
                *error = msg.str();
            }
            return false;
        }

        if (fn.name == "main") {
            std::string entry_out = out_dir + "/1.b";
            if (!WriteMirAsV1Bytecode(one, "1", entry_out, error)) {
                if (error) {
                    *error = "failed to emit entry alias 1.b";
                }
                return false;
            }
        }
    }

    if (!module.functions.empty()) {
        MirModule empty;
        std::string sfun_out = out_dir + "/rc/simulate_efun.b";
        if (!WriteMirAsV1Bytecode(empty, "rc/simulate_efun", sfun_out, error)) {
            if (error) {
                *error = "failed to emit simulate_efun stub bytecode";
            }
            return false;
        }
    }
    return true;
}

bool WriteMirModuleSetAsV1BytecodeAtPath(
    const MirModule &module,
    const std::string &source_path,
    const std::string &workspace_root,
    const std::string &out_root,
    std::string *error,
    std::string *out_module_name) {
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

    std::string stem = src.stem().string();
    std::string module_name = src.stem().string();
    fs::path rel_file;
    try {
        rel_file = fs::relative(src, ws);
    } catch (...) {
        rel_file = src.filename();
    }
    module_name = rel_file.replace_extension("").generic_string();
    MirModule one;
    one.class_order = module.class_order;
    one.class_fields = module.class_fields;
    one.global_variables = module.global_variables;
    one.init_function = module.init_function;
    one.functions = module.functions;

    const fs::path out_file = out_dir / (stem + ".b");
    if (!WriteMirAsV1Bytecode(one, module_name, out_file.string(), error)) {
        return false;
    }

    if (out_module_name) {
        *out_module_name = module_name;
    }

    // keep runtime bootstrap compatibility for default sfun object
    fs::path sfun_dir = bin / "rc";
    fs::create_directories(sfun_dir, ec);
    if (!ec) {
        MirModule empty;
        WriteMirAsV1Bytecode(empty, "rc/simulate_efun", (sfun_dir / "simulate_efun.b").string(), nullptr);
    }

    return true;
}

} // namespace frontend2
} // namespace lpc
