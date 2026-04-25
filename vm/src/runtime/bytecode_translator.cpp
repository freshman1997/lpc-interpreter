#include "runtime/bytecode_translator.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "opcode.h"
#include "runtime/vm.h"
#include "nextvm/bytecode/opcode.h"
#include "nextvm/runtime/vm.h"
#include "nextvm/value/value.h"

extern std::string get_cwd();

namespace {

struct V1FuncMeta {
    std::string name;
    std::uint16_t nargs = 0;
    std::uint16_t nlocals = 0;
    std::uint32_t from = 0;
    std::uint32_t to = 0;
};

static bool ReadU8(std::ifstream &in, std::uint8_t *v) {
    char b = 0;
    in.read(&b, 1);
    if (!in.good()) {
        return false;
    }
    *v = static_cast<std::uint8_t>(b);
    return true;
}

static bool ReadU16(std::ifstream &in, std::uint16_t *v) {
    unsigned char b[2] = {0, 0};
    in.read(reinterpret_cast<char *>(b), 2);
    if (!in.good()) {
        return false;
    }
    *v = static_cast<std::uint16_t>(b[0]) | static_cast<std::uint16_t>(b[1] << 8);
    return true;
}

static bool ReadU32(std::ifstream &in, std::uint32_t *v) {
    unsigned char b[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char *>(b), 4);
    if (!in.good()) {
        return false;
    }
    *v = static_cast<std::uint32_t>(b[0]) |
        static_cast<std::uint32_t>(b[1] << 8) |
        static_cast<std::uint32_t>(b[2] << 16) |
        static_cast<std::uint32_t>(b[3] << 24);
    return true;
}

static bool ReadString(std::ifstream &in, std::string *out) {
    std::uint32_t n = 0;
    if (!ReadU32(in, &n)) {
        return false;
    }
    out->assign(n, '\0');
    if (n > 0) {
        in.read(&(*out)[0], static_cast<std::streamsize>(n));
        if (!in.good()) {
            return false;
        }
    }
    return true;
}

static bool ReadU64(std::ifstream &in, std::uint64_t *v) {
    unsigned char b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    in.read(reinterpret_cast<char *>(b), 8);
    if (!in.good()) {
        return false;
    }
    *v = 0;
    for (int i = 0; i < 8; ++i) {
        *v |= static_cast<std::uint64_t>(b[i]) << (i * 8);
    }
    return true;
}

static std::string ResolveModuleBytecodePath(const std::string &entry_module) {
    const std::string cwd = get_cwd();
    const std::string p1 = cwd + "/bin/" + entry_module + ".b";
    const std::string p2 = cwd + "/build/compiler/" + entry_module + ".b";
    const std::string p3 = cwd + "/../build/compiler/" + entry_module + ".b";

    std::ifstream in1(p1.c_str(), std::ios::binary);
    if (in1.good()) return p1;
    std::ifstream in2(p2.c_str(), std::ios::binary);
    if (in2.good()) return p2;
    std::ifstream in3(p3.c_str(), std::ios::binary);
    if (in3.good()) return p3;
    return "";
}

static std::string ResolveModuleNextVmPath(const std::string &entry_module) {
    const std::string cwd = get_cwd();
    const std::string p1 = cwd + "/bin/" + entry_module + ".nb";
    const std::string p2 = cwd + "/build/compiler/" + entry_module + ".nb";
    const std::string p3 = cwd + "/../build/compiler/" + entry_module + ".nb";

    std::ifstream in1(p1.c_str(), std::ios::binary);
    if (in1.good()) return p1;
    std::ifstream in2(p2.c_str(), std::ios::binary);
    if (in2.good()) return p2;
    std::ifstream in3(p3.c_str(), std::ios::binary);
    if (in3.good()) return p3;
    return "";
}

static lpc::core::Status LoadNextVmChunk(const std::string &path, lpc::nextvm::Chunk *chunk) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.good()) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::NotFound, "nextvm bytecode file not found");
    }

    char magic[8] = {};
    in.read(magic, 8);
    if (!in.good() || std::memcmp(magic, "LPCNVM1", 7) != 0 || magic[7] != 0) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm bytecode magic");
    }

    lpc::nextvm::Chunk out;
    if (!ReadString(in, &out.module_name)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm module name");
    }

    std::uint32_t count = 0;
    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm int const header");
    }
    out.iconst.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t bits = 0;
        if (!ReadU64(in, &bits)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm int const");
        }
        out.iconst.push_back(static_cast<std::int64_t>(bits));
    }

    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm float const header");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t bits = 0;
        if (!ReadU64(in, &bits)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm float const");
        }
        double v = 0.0;
        static_assert(sizeof(double) == sizeof(std::uint64_t), "double must be 64 bits");
        std::memcpy(&v, &bits, sizeof(double));
        out.fconst.push_back(v);
    }

    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm string const header");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string s;
        if (!ReadString(in, &s)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm string const");
        }
        out.sconst.push_back(s);
    }

    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm class header");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t nfields = 0;
        if (!ReadU16(in, &nfields)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm class field count");
        }
        out.class_field_counts.push_back(nfields);
    }

    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm function header");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        lpc::nextvm::FunctionProto f;
        if (!ReadString(in, &f.name) ||
            !ReadU16(in, &f.arity) ||
            !ReadU16(in, &f.nlocals) ||
            !ReadU16(in, &f.max_stack) ||
            !ReadU32(in, &f.code_start) ||
            !ReadU32(in, &f.code_end)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm function entry");
        }
        out.functions.push_back(f);
    }

    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm line header");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        lpc::nextvm::LineEntry e;
        if (!ReadU32(in, &e.line) || !ReadU32(in, &e.pc)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm line entry");
        }
        out.line_table.push_back(e);
    }

    if (!ReadU32(in, &count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm code header");
    }
    out.code.assign(count, 0);
    if (count > 0) {
        in.read(reinterpret_cast<char *>(out.code.data()), static_cast<std::streamsize>(count));
        if (!in.good()) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid nextvm code payload");
        }
    }

    *chunk = std::move(out);
    return lpc::core::Status::OkStatus();
}

static lpc::core::Status LoadV1Minimal(
    const std::string &path,
    std::string *module_name,
    std::vector<V1FuncMeta> *funcs,
    std::vector<std::int64_t> *iconst,
    std::vector<std::uint8_t> *instructions) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.good()) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::NotFound, "bytecode file not found");
    }

    if (!ReadString(in, module_name)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid module name header");
    }

    std::uint32_t class_count = 0;
    if (!ReadU32(in, &class_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid class section header");
    }
    for (std::uint32_t i = 0; i < class_count; ++i) {
        std::string cname;
        if (!ReadString(in, &cname)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid class name");
        }
        std::uint8_t is_static = 0;
        std::uint16_t nfield = 0;
        if (!ReadU8(in, &is_static) || !ReadU16(in, &nfield)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid class header");
        }
    }

    std::uint16_t tmp16 = 0;
    if (!ReadU16(in, &tmp16) || !ReadU16(in, &tmp16) || !ReadU16(in, &tmp16)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid hook section");
    }

    std::uint32_t line_count = 0;
    if (!ReadU32(in, &line_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid line map header");
    }
    for (std::uint32_t i = 0; i < line_count; ++i) {
        std::uint32_t a = 0, b = 0;
        if (!ReadU32(in, &a) || !ReadU32(in, &b)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid line map entry");
        }
    }

    std::uint32_t func_count = 0;
    if (!ReadU32(in, &func_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid function table header");
    }

    funcs->clear();
    funcs->reserve(func_count);
    for (std::uint32_t i = 0; i < func_count; ++i) {
        V1FuncMeta f;
        if (!ReadString(in, &f.name)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid function name");
        }
        std::uint8_t ret = 0;
        std::uint8_t st = 0;
        std::uint16_t nup = 0;
        if (!ReadU8(in, &ret) || !ReadU8(in, &st) || !ReadU16(in, &f.nargs) || !ReadU16(in, &f.nlocals) || !ReadU16(in, &nup)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid function header");
        }
        for (std::uint16_t ui = 0; ui < nup; ++ui) {
            if (!ReadU16(in, &tmp16) || !ReadU16(in, &tmp16)) {
                return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid upvalue header");
            }
        }
        if (!ReadU32(in, &f.from) || !ReadU32(in, &f.to)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid function pc range");
        }
        funcs->push_back(f);
    }

    std::uint32_t nvar = 0;
    if (!ReadU32(in, &nvar)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid var header");
    }
    if (nvar > 0) {
        std::vector<char> flags(static_cast<std::size_t>(nvar));
        in.read(flags.data(), static_cast<std::streamsize>(nvar));
        if (!in.good()) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid var flags");
        }
    }

    std::uint32_t iconst_count = 0;
    if (!ReadU32(in, &iconst_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid iconst header");
    }
    iconst->clear();
    iconst->reserve(iconst_count);
    for (std::uint32_t i = 0; i < iconst_count; ++i) {
        std::uint32_t v = 0;
        if (!ReadU32(in, &v)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid iconst entry");
        }
        iconst->push_back(static_cast<std::int32_t>(v));
    }

    std::uint32_t fconst_count = 0;
    if (!ReadU32(in, &fconst_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid fconst header");
    }
    if (fconst_count > 0) {
        in.seekg(static_cast<std::streamoff>(fconst_count * 4), std::ios::cur);
        if (!in.good()) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid fconst body");
        }
    }

    std::uint32_t sconst_count = 0;
    if (!ReadU32(in, &sconst_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid sconst header");
    }
    for (std::uint32_t i = 0; i < sconst_count; ++i) {
        std::string s;
        if (!ReadString(in, &s)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid sconst entry");
        }
    }

    std::uint8_t has_clazz = 0;
    if (!ReadU8(in, &has_clazz)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid class flag");
    }
    if (has_clazz == 0) {
        std::uint32_t cc = 0;
        if (!ReadU32(in, &cc)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid class table header");
        }
        for (std::uint32_t i = 0; i < cc; ++i) {
            std::uint8_t b = 0;
            if (!ReadU8(in, &b) || !ReadU16(in, &tmp16)) {
                return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid class table entry");
            }
        }
    }

    std::uint32_t init_size = 0;
    if (!ReadU32(in, &init_size)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid init code header");
    }
    if (init_size > 0) {
        in.seekg(static_cast<std::streamoff>(init_size), std::ios::cur);
        if (!in.good()) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid init code body");
        }
    }

    std::uint32_t init_line_count = 0;
    if (!ReadU32(in, &init_line_count)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid init line header");
    }
    for (std::uint32_t i = 0; i < init_line_count; ++i) {
        std::uint32_t a = 0, b = 0;
        if (!ReadU32(in, &a) || !ReadU32(in, &b)) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid init line entry");
        }
    }

    std::uint32_t code_size = 0;
    if (!ReadU32(in, &code_size)) {
        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid instruction header");
    }
    instructions->assign(code_size, 0);
    if (code_size > 0) {
        in.read(reinterpret_cast<char *>(instructions->data()), static_cast<std::streamsize>(code_size));
        if (!in.good()) {
            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "invalid instruction body");
        }
    }

    return lpc::core::Status::OkStatus();
}

static int EnsureIConst(std::vector<std::int64_t> *iconst, std::int64_t v) {
    for (int i = 0; i < static_cast<int>(iconst->size()); ++i) {
        if ((*iconst)[i] == v) {
            return i;
        }
    }
    iconst->push_back(v);
    return static_cast<int>(iconst->size() - 1);
}

static void EmitU16(std::vector<std::uint8_t> *code, std::uint16_t v) {
    code->push_back(static_cast<std::uint8_t>(v & 0xff));
    code->push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

static lpc::core::Status TranslateV1ToNextVM(
    const std::string &module_name,
    const std::vector<V1FuncMeta> &v1_funcs,
    const std::vector<std::int64_t> &v1_iconst,
    const std::vector<std::uint8_t> &v1_code,
    lpc::nextvm::Chunk *out) {
    out->module_name = module_name;
    out->code.clear();
    out->iconst = v1_iconst;
    out->functions.clear();
    out->class_field_counts.clear();

    std::unordered_map<std::string, std::uint16_t> func_name_to_nextvm;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(v1_funcs.size()); ++i) {
        func_name_to_nextvm[v1_funcs[i].name] = i;
    }

    std::unordered_map<std::uint32_t, std::uint16_t> from_pc_to_v1_index;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(v1_funcs.size()); ++i) {
        from_pc_to_v1_index[v1_funcs[i].from] = i;
    }

    for (int fi = 0; fi < static_cast<int>(v1_funcs.size()); ++fi) {
        const V1FuncMeta &vf = v1_funcs[fi];
        lpc::nextvm::FunctionProto nf;
        nf.name = vf.name;
        nf.arity = vf.nargs;
        nf.nlocals = vf.nlocals;
        nf.code_start = static_cast<std::uint32_t>(out->code.size());

        std::unordered_map<std::uint32_t, std::uint32_t> old_to_new;
        struct Patch {
            std::uint32_t pos = 0;
            std::uint32_t target_old = 0;
        };
        std::vector<Patch> patches;

        std::uint32_t ip = vf.from;
        bool has_return = false;
        while (ip < vf.to) {
            const std::uint32_t old_pc = ip;
            old_to_new[old_pc] = static_cast<std::uint32_t>(out->code.size());
            std::uint8_t op = v1_code[ip++];

            if (op == static_cast<std::uint8_t>(OpCode::op_load_iconst)) {
                if (ip + 2 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated op_load_iconst");
                }
                std::uint16_t idx = static_cast<std::uint16_t>(v1_code[ip]) |
                    static_cast<std::uint16_t>(v1_code[ip + 1] << 8);
                ip += 2;
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LoadIConst));
                EmitU16(&out->code, idx);
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_load_0) || op == static_cast<std::uint8_t>(OpCode::op_load_1)) {
                const int idx = EnsureIConst(&out->iconst, op == static_cast<std::uint8_t>(OpCode::op_load_0) ? 0 : 1);
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LoadIConst));
                EmitU16(&out->code, static_cast<std::uint16_t>(idx));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_load_local) || op == static_cast<std::uint8_t>(OpCode::op_store_local)) {
                if (ip + 2 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated local op");
                }
                std::uint16_t idx = static_cast<std::uint16_t>(v1_code[ip]) |
                    static_cast<std::uint16_t>(v1_code[ip + 1] << 8);
                ip += 2;
                out->code.push_back(static_cast<std::uint8_t>(
                    op == static_cast<std::uint8_t>(OpCode::op_load_local)
                        ? lpc::nextvm::Op::LoadLocal
                        : lpc::nextvm::Op::StoreLocal));
                EmitU16(&out->code, idx);
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_load_func)) {
                if (ip + 2 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated load_func op");
                }
                std::uint16_t idx = static_cast<std::uint16_t>(v1_code[ip]) |
                    static_cast<std::uint16_t>(v1_code[ip + 1] << 8);
                ip += 2;
                if (idx >= v1_funcs.size()) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "load_func index out of range");
                }
                std::uint16_t mapped_idx = idx;
                const int cidx = EnsureIConst(&out->iconst, static_cast<std::int64_t>(mapped_idx));
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LoadIConst));
                EmitU16(&out->code, static_cast<std::uint16_t>(cidx));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_add)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Add));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_sub)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Sub));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_mul)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Mul));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_div)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Div));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_mod)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Mod));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_eq)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Eq));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_neq)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Neq));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_gt)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Gt));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_gte)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Gte));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_lt)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Lt));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_lte)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Lte));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_and)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LogicAnd));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_or)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LogicOr));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_cmp_not)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LogicNot));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_new_class)) {
                if (ip + 2 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated new_class op");
                }
                std::uint16_t class_idx = static_cast<std::uint16_t>(v1_code[ip]) |
                    static_cast<std::uint16_t>(v1_code[ip + 1] << 8);
                ip += 2;
                while (out->class_field_counts.size() <= class_idx) {
                    out->class_field_counts.push_back(0);
                }
                if (out->class_field_counts[class_idx] == 0) {
                    out->class_field_counts[class_idx] = 16;
                }
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::NewClass));
                EmitU16(&out->code, class_idx);
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_set_class_field)) {
                if (ip + 2 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated set_class_field op");
                }
                std::uint16_t field_idx = static_cast<std::uint16_t>(v1_code[ip]) |
                    static_cast<std::uint16_t>(v1_code[ip + 1] << 8);
                ip += 2;
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::SetClassField));
                EmitU16(&out->code, field_idx);
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_load_class_field)) {
                if (ip + 2 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated load_class_field op");
                }
                std::uint16_t field_idx = static_cast<std::uint16_t>(v1_code[ip]) |
                    static_cast<std::uint16_t>(v1_code[ip + 1] << 8);
                ip += 2;
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LoadClassField));
                EmitU16(&out->code, field_idx);
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_new_array)) {
                if (ip + 4 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated new_array op");
                }
                std::uint32_t n = static_cast<std::uint32_t>(v1_code[ip]) |
                    static_cast<std::uint32_t>(v1_code[ip + 1] << 8) |
                    static_cast<std::uint32_t>(v1_code[ip + 2] << 16) |
                    static_cast<std::uint32_t>(v1_code[ip + 3] << 24);
                ip += 4;
                if (n > 0xffff) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "new_array too large for NextVM operand");
                }
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::NewArray));
                EmitU16(&out->code, static_cast<std::uint16_t>(n));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_index)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Index));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_return)) {
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Return));
                has_return = true;
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_call)) {
                if (ip + 1 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated call op type");
                }
                std::uint8_t type = v1_code[ip++];
                if (type != 0) {
                    if (ip + 2 > vf.to) {
                        return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated call op index");
                    }
                    ip += 2;
                    if (type == 1) {
                        if (ip + 1 > vf.to) {
                            return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated efun argc");
                        }
                        ip += 1;
                    }
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "NextVM translator only supports local call type=0");
                }

                if (out->code.size() < 3 || out->code[out->code.size() - 3] != static_cast<std::uint8_t>(lpc::nextvm::Op::LoadIConst)) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "call without preceding callee const in translator");
                }
                std::uint16_t cidx = static_cast<std::uint16_t>(out->code[out->code.size() - 2]) |
                    static_cast<std::uint16_t>(out->code[out->code.size() - 1] << 8);
                if (cidx >= out->iconst.size()) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "call callee const index out of range");
                }
                std::int64_t callee_idx = out->iconst[cidx];
                if (callee_idx < 0 || callee_idx >= static_cast<std::int64_t>(v1_funcs.size())) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "call callee function index out of range");
                }

                const std::string &callee_name = v1_funcs[static_cast<std::size_t>(callee_idx)].name;
                if (!func_name_to_nextvm.count(callee_name)) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "call callee function name not found in NextVM map");
                }
                std::uint16_t mapped_callee = func_name_to_nextvm[callee_name];

                if (mapped_callee == static_cast<std::uint16_t>(fi)) {
                    std::string msg = "self-recursive call is not supported in NextVM translator yet: caller=" +
                        v1_funcs[fi].name + " callee=" + callee_name;
                    return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, msg);
                }

                out->code.erase(out->code.end() - 3, out->code.end());
                out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::CallValue));
                EmitU16(&out->code, mapped_callee);
                EmitU16(&out->code, static_cast<std::uint16_t>(v1_funcs[static_cast<std::size_t>(callee_idx)].nargs));
                continue;
            }

            if (op == static_cast<std::uint8_t>(OpCode::op_goto) || op == static_cast<std::uint8_t>(OpCode::op_test)) {
                if (ip + 4 > vf.to) {
                    return lpc::core::Status::Error(lpc::core::ErrorCode::ParseError, "truncated jump op");
                }
                std::uint32_t target = static_cast<std::uint32_t>(v1_code[ip]) |
                    static_cast<std::uint32_t>(v1_code[ip + 1] << 8) |
                    static_cast<std::uint32_t>(v1_code[ip + 2] << 16) |
                    static_cast<std::uint32_t>(v1_code[ip + 3] << 24);
                ip += 4;
                out->code.push_back(static_cast<std::uint8_t>(
                    op == static_cast<std::uint8_t>(OpCode::op_goto)
                        ? lpc::nextvm::Op::Jump
                        : lpc::nextvm::Op::JumpIfFalse));
                std::uint32_t patch_pos = static_cast<std::uint32_t>(out->code.size());
                EmitU16(&out->code, 0);
                patches.push_back({patch_pos, target});
                continue;
            }

            std::string msg = "unsupported v1 opcode in NextVM translator: " + std::to_string(static_cast<int>(op));
            return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, msg);
        }

        old_to_new[vf.to] = static_cast<std::uint32_t>(out->code.size());
        if (!has_return) {
            old_to_new[vf.to] = static_cast<std::uint32_t>(out->code.size());
            out->code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Return));
        }

        for (const Patch &p : patches) {
            if (!old_to_new.count(p.target_old)) {
                return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "jump target not aligned in translator");
            }
            const std::int64_t target_new = static_cast<std::int64_t>(old_to_new[p.target_old]);
            const std::int64_t base = static_cast<std::int64_t>(p.pos + 2);
            const std::int64_t rel = target_new - base;
            if (rel < -32768 || rel > 32767) {
                return lpc::core::Status::Error(lpc::core::ErrorCode::VmError, "jump target too far for NextVM rel16");
            }
            std::uint16_t bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(rel));
            out->code[p.pos + 0] = static_cast<std::uint8_t>(bits & 0xff);
            out->code[p.pos + 1] = static_cast<std::uint8_t>((bits >> 8) & 0xff);
        }

        nf.code_end = static_cast<std::uint32_t>(out->code.size());
        out->functions.push_back(nf);
    }

    return lpc::core::Status::OkStatus();
}

} // namespace

namespace lpc {
namespace runtime {

core::Status RunEntryModuleNextVM(const std::string &entry_module) {
    nextvm::Chunk ch;
    const std::string next_path = ResolveModuleNextVmPath(entry_module);
    if (!next_path.empty()) {
        core::Status direct = LoadNextVmChunk(next_path, &ch);
        if (!direct.ok()) {
            return direct;
        }
    } else {
    const std::string path = ResolveModuleBytecodePath(entry_module);
    if (path.empty()) {
        return core::Status::Error(core::ErrorCode::NotFound, "NextVM could not find module bytecode");
    }

    std::string module_name;
    std::vector<V1FuncMeta> funcs;
    std::vector<std::int64_t> iconst;
    std::vector<std::uint8_t> code;
    core::Status s = LoadV1Minimal(path, &module_name, &funcs, &iconst, &code);
    if (!s.ok()) {
        return s;
    }

    s = TranslateV1ToNextVM(module_name, funcs, iconst, code, &ch);
    if (!s.ok()) {
        return s;
    }
    }

    nextvm::Vm nextvm_engine;
    nextvm::RuntimeError e = nextvm_engine.LoadChunk(ch);
    if (!e.ok()) {
        return core::Status::Error(core::ErrorCode::VmError, "NextVM load chunk failed: " + e.message);
    }
    e = nextvm_engine.RunEntry("main");
    if (!e.ok()) {
        return core::Status::Error(core::ErrorCode::VmError, "NextVM run failed: " + e.message);
    }

    nextvm::Value out = nextvm_engine.last_result();
    if (out.tag == nextvm::ValueTag::Int64) {
        std::cout << "NextVM result: " << out.as.i64 << std::endl;
    }
    return core::Status::OkStatus();
}

core::Status RunEntryModuleCompare(const std::string &entry_module) {
    std::cout << "[compare] protocol: NextVM-first, legacy-inprocess-best-effort" << std::endl;

    const std::string path = ResolveModuleBytecodePath(entry_module);
    if (path.empty()) {
        return core::Status::Error(core::ErrorCode::NotFound, "NextVM could not find module bytecode");
    }

    std::string module_name;
    std::vector<V1FuncMeta> funcs;
    std::vector<std::int64_t> iconst;
    std::vector<std::uint8_t> code;
    core::Status s = LoadV1Minimal(path, &module_name, &funcs, &iconst, &code);
    if (!s.ok()) {
        return s;
    }

    nextvm::Chunk ch;
    s = TranslateV1ToNextVM(module_name, funcs, iconst, code, &ch);
    if (!s.ok()) {
        return s;
    }

    nextvm::Vm nextvm_engine;
    nextvm::RuntimeError e = nextvm_engine.LoadChunk(ch);
    if (!e.ok()) {
        std::cout << "[compare] NextVM_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "NextVM load chunk failed: " + e.message);
    }
    e = nextvm_engine.RunEntry("main");
    if (!e.ok()) {
        std::cout << "[compare] NextVM_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "NextVM run failed: " + e.message);
    }

    if (!s.ok()) {
        std::cout << "[compare] NextVM_status=error" << std::endl;
        return s;
    }
    std::cout << "[compare] NextVM_status=ok" << std::endl;

    std::unique_ptr<lpc_vm_t> legacy(lpc_vm_t::create_vm());
    legacy->set_non_fatal_mode(true);
    legacy->set_memory_limit_bytes(2ULL * 1024ULL * 1024ULL * 1024ULL);
    legacy->set_entry(entry_module.c_str());
    try {
        legacy->bootstrap();
    } catch (const std::bad_alloc &) {
        std::cout << "[compare] legacy_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "legacy bootstrap failed: bad_alloc");
    } catch (...) {
        std::cout << "[compare] legacy_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "legacy bootstrap failed: unknown exception");
    }
    if (legacy->has_error()) {
        std::cout << "[compare] legacy_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "legacy bootstrap failed: " + legacy->last_error());
    }

    try {
        legacy->run_main();
    } catch (const std::bad_alloc &) {
        std::cout << "[compare] legacy_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "legacy run failed: bad_alloc");
    } catch (...) {
        std::cout << "[compare] legacy_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "legacy run failed: unknown exception");
    }
    if (legacy->has_error()) {
        std::cout << "[compare] legacy_status=error" << std::endl;
        return core::Status::Error(core::ErrorCode::VmError, "legacy run failed: " + legacy->last_error());
    }

    std::cout << "[compare] legacy_status=ok" << std::endl;

    nextvm::Value v2 = nextvm_engine.last_result();
    if (v2.tag == nextvm::ValueTag::Int64 && legacy->has_last_int_result()) {
        std::cout << "[compare] NextVM_result_int=" << v2.as.i64 << std::endl;
        std::cout << "[compare] legacy_result_int=" << legacy->last_int_result() << std::endl;
        if (static_cast<lint64_t>(legacy->last_int_result()) != v2.as.i64) {
            return core::Status::Error(core::ErrorCode::VmError, "result mismatch between NextVM and legacy");
        }
        std::cout << "[compare] result_match=ok" << std::endl;
    } else {
        std::cout << "[compare] result_match=skipped" << std::endl;
    }

    std::cout << "[compare] done" << std::endl;
    return core::Status::OkStatus();
}

} // namespace runtime
} // namespace lpc
