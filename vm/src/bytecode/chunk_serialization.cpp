#include "vm/bytecode/binary_format.h"
#include "vm/bytecode/chunk.h"
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

namespace lpc {
namespace vm {

static void WriteU8(std::vector<std::uint8_t> &buf, std::uint8_t v) {
    buf.push_back(v);
}

static void WriteU16(std::vector<std::uint8_t> &buf, std::uint16_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

static void WriteU32(std::vector<std::uint8_t> &buf, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

static void WriteI64(std::vector<std::uint8_t> &buf, std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xFF));
}

static void WriteF64(std::vector<std::uint8_t> &buf, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, 8);
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFF));
}

static void WriteStr(std::vector<std::uint8_t> &buf, const std::string &s) {
    WriteU32(buf, static_cast<std::uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

static void WriteBytes(std::vector<std::uint8_t> &buf, const std::uint8_t *data, std::size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static bool ReadU8(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint8_t &out) {
    if (pos >= len) return false;
    out = data[pos++];
    return true;
}

static bool ReadU16(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint16_t &out) {
    if (pos + 2 > len) return false;
    out = static_cast<std::uint16_t>(data[pos]) | (static_cast<std::uint16_t>(data[pos + 1]) << 8);
    pos += 2;
    return true;
}

static bool ReadU32(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint32_t &out) {
    if (pos + 4 > len) return false;
    out = 0;
    for (int i = 0; i < 4; ++i)
        out |= static_cast<std::uint32_t>(data[pos + i]) << (i * 8);
    pos += 4;
    return true;
}

static bool ReadI64(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::int64_t &out) {
    if (pos + 8 > len) return false;
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= static_cast<std::uint64_t>(data[pos + i]) << (i * 8);
    pos += 8;
    out = static_cast<std::int64_t>(u);
    return true;
}

static bool ReadF64(const std::uint8_t *data, std::size_t len, std::size_t &pos, double &out) {
    if (pos + 8 > len) return false;
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t>(data[pos + i]) << (i * 8);
    pos += 8;
    std::memcpy(&out, &bits, 8);
    return true;
}

static bool ReadStr(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::string &out) {
    std::uint32_t slen = 0;
    if (!ReadU32(data, len, pos, slen)) return false;
    if (pos + slen > len) return false;
    out.assign(reinterpret_cast<const char *>(data + pos), slen);
    pos += slen;
    return true;
}

static bool ReadBytes(const std::uint8_t *data, std::size_t len, std::size_t &pos,
                       std::vector<std::uint8_t> &out, std::size_t count) {
    if (pos + count > len) return false;
    out.assign(data + pos, data + pos + count);
    pos += count;
    return true;
}

static void WriteFieldDefault(std::vector<std::uint8_t> &buf, const ClassInfo::FieldDefault &def) {
    WriteU8(buf, static_cast<std::uint8_t>(def.kind));
    switch (def.kind) {
    case ClassInfo::FieldDefault::Kind::Int:
        WriteI64(buf, def.int_value);
        break;
    case ClassInfo::FieldDefault::Kind::Float:
        WriteF64(buf, def.float_value);
        break;
    case ClassInfo::FieldDefault::Kind::String:
        WriteStr(buf, def.string_value);
        break;
    case ClassInfo::FieldDefault::Kind::Mapping:
        WriteU32(buf, static_cast<std::uint32_t>(def.mapping_pairs.size()));
        for (const auto &pair : def.mapping_pairs) {
            WriteFieldDefault(buf, pair.first);
            WriteFieldDefault(buf, pair.second);
        }
        break;
    case ClassInfo::FieldDefault::Kind::Array:
        WriteU32(buf, static_cast<std::uint32_t>(def.array_items.size()));
        for (const auto &item : def.array_items) {
            WriteFieldDefault(buf, item);
        }
        break;
    case ClassInfo::FieldDefault::Kind::Zero:
    default:
        break;
    }
}

static bool ReadFieldDefault(const std::uint8_t *data, std::size_t len, std::size_t &pos, ClassInfo::FieldDefault &def) {
    std::uint8_t kind = 0;
    if (!ReadU8(data, len, pos, kind)) return false;
    def.kind = static_cast<ClassInfo::FieldDefault::Kind>(kind);
    switch (def.kind) {
    case ClassInfo::FieldDefault::Kind::Int:
        return ReadI64(data, len, pos, def.int_value);
    case ClassInfo::FieldDefault::Kind::Float:
        return ReadF64(data, len, pos, def.float_value);
    case ClassInfo::FieldDefault::Kind::String:
        return ReadStr(data, len, pos, def.string_value);
    case ClassInfo::FieldDefault::Kind::Mapping: {
        std::uint32_t count = 0;
        if (!ReadU32(data, len, pos, count)) return false;
        def.mapping_pairs.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!ReadFieldDefault(data, len, pos, def.mapping_pairs[i].first)) return false;
            if (!ReadFieldDefault(data, len, pos, def.mapping_pairs[i].second)) return false;
        }
        return true;
    }
    case ClassInfo::FieldDefault::Kind::Array: {
        std::uint32_t count = 0;
        if (!ReadU32(data, len, pos, count)) return false;
        def.array_items.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!ReadFieldDefault(data, len, pos, def.array_items[i])) return false;
        }
        return true;
    }
    case ClassInfo::FieldDefault::Kind::Zero:
    default:
        def.kind = ClassInfo::FieldDefault::Kind::Zero;
        return true;
    }
}

std::string SerializeChunk(const Chunk &chunk) {
    std::vector<std::uint8_t> buf;

    buf.push_back(0x4C); buf.push_back(0x50);
    buf.push_back(0x43); buf.push_back(0x00);
    WriteU32(buf, 2);

    WriteStr(buf, chunk.module_name);
    WriteU32(buf, chunk.flags);
    WriteStr(buf, chunk.source_file);

    WriteU32(buf, static_cast<std::uint32_t>(chunk.iconst.size()));
    for (const auto &v : chunk.iconst) WriteI64(buf, v);

    WriteU32(buf, static_cast<std::uint32_t>(chunk.fconst.size()));
    for (const auto &v : chunk.fconst) WriteF64(buf, v);

    WriteU32(buf, static_cast<std::uint32_t>(chunk.sconst.size()));
    for (const auto &s : chunk.sconst) WriteStr(buf, s);

    WriteU32(buf, static_cast<std::uint32_t>(chunk.classes.size()));
    for (const auto &cls : chunk.classes) {
        WriteStr(buf, cls.name);
        WriteU16(buf, cls.nfields);
        WriteU16(buf, cls.parent_class_idx);
        WriteU32(buf, static_cast<std::uint32_t>(cls.field_names.size()));
        for (std::size_t i = 0; i < cls.field_names.size(); ++i) {
            WriteStr(buf, cls.field_names[i]);
            ClassInfo::FieldDefault def;
            if (i < cls.field_defaults.size()) def = cls.field_defaults[i];
            WriteFieldDefault(buf, def);
        }
    }

    WriteU32(buf, static_cast<std::uint32_t>(chunk.functions.size()));
    for (const auto &fn : chunk.functions) {
        WriteStr(buf, fn.name);
        WriteU16(buf, fn.arity);
        WriteU16(buf, fn.nlocals);
        WriteU16(buf, fn.max_stack);
        WriteU32(buf, fn.code_start);
        WriteU32(buf, fn.code_end);
        WriteU16(buf, static_cast<std::uint16_t>(fn.upvalues.size()));
        for (const auto &uv : fn.upvalues) {
            WriteU16(buf, uv.source_kind);
            WriteU16(buf, uv.source_index);
        }
    }

    WriteU32(buf, static_cast<std::uint32_t>(chunk.line_table.size()));
    for (const auto &le : chunk.line_table) {
        WriteU32(buf, le.line);
        WriteU32(buf, le.pc);
    }

    WriteU32(buf, static_cast<std::uint32_t>(chunk.code.size()));
    if (!chunk.code.empty()) WriteBytes(buf, chunk.code.data(), chunk.code.size());

    WriteU32(buf, static_cast<std::uint32_t>(chunk.init_code.size()));
    if (!chunk.init_code.empty()) WriteBytes(buf, chunk.init_code.data(), chunk.init_code.size());

    WriteU32(buf, static_cast<std::uint32_t>(chunk.global_names.size()));
    for (const auto &gn : chunk.global_names) WriteStr(buf, gn);

    WriteStr(buf, chunk.debug_info.source_file);
    WriteU32(buf, static_cast<std::uint32_t>(chunk.debug_info.global_names.size()));
    for (const auto &gn : chunk.debug_info.global_names) WriteStr(buf, gn);
    WriteU32(buf, static_cast<std::uint32_t>(chunk.debug_info.function_debug.size()));
    for (const auto &fd : chunk.debug_info.function_debug) {
        WriteU16(buf, static_cast<std::uint16_t>(fd.param_names.size()));
        for (const auto &p : fd.param_names) WriteStr(buf, p);
        WriteU16(buf, static_cast<std::uint16_t>(fd.local_names.size()));
        for (const auto &l : fd.local_names) WriteStr(buf, l);
        WriteU16(buf, static_cast<std::uint16_t>(fd.upvalue_names.size()));
        for (const auto &u : fd.upvalue_names) WriteStr(buf, u);
    }

    return std::string(reinterpret_cast<const char *>(buf.data()), buf.size());
}

bool DeserializeChunk(const std::string &data, Chunk *out_chunk) {
    if (!out_chunk) return false;
    const std::uint8_t *d = reinterpret_cast<const std::uint8_t *>(data.data());
    std::size_t len = data.size();
    std::size_t pos = 0;

    if (len < 8) return false;
    if (d[0] != 0x4C || d[1] != 0x50 || d[2] != 0x43) return false;
    pos = 4;
    std::uint32_t version = 0;
    if (!ReadU32(d, len, pos, version)) return false;
    if (version != 2) return false;

    if (!ReadStr(d, len, pos, out_chunk->module_name)) return false;
    if (!ReadU32(d, len, pos, out_chunk->flags)) return false;
    if (!ReadStr(d, len, pos, out_chunk->source_file)) return false;

    std::uint32_t n_iconst = 0;
    if (!ReadU32(d, len, pos, n_iconst)) return false;
    out_chunk->iconst.resize(n_iconst);
    for (std::size_t i = 0; i < n_iconst; ++i) {
        if (!ReadI64(d, len, pos, out_chunk->iconst[i])) return false;
    }

    std::uint32_t n_fconst = 0;
    if (!ReadU32(d, len, pos, n_fconst)) return false;
    out_chunk->fconst.resize(n_fconst);
    for (std::size_t i = 0; i < n_fconst; ++i) {
        if (!ReadF64(d, len, pos, out_chunk->fconst[i])) return false;
    }

    std::uint32_t n_sconst = 0;
    if (!ReadU32(d, len, pos, n_sconst)) return false;
    out_chunk->sconst.resize(n_sconst);
    for (std::size_t i = 0; i < n_sconst; ++i) {
        if (!ReadStr(d, len, pos, out_chunk->sconst[i])) return false;
    }

    std::uint32_t n_classes = 0;
    if (!ReadU32(d, len, pos, n_classes)) return false;
    out_chunk->classes.resize(n_classes);
    for (std::size_t i = 0; i < n_classes; ++i) {
        auto &cls = out_chunk->classes[i];
        if (!ReadStr(d, len, pos, cls.name)) return false;
        if (!ReadU16(d, len, pos, cls.nfields)) return false;
        if (!ReadU16(d, len, pos, cls.parent_class_idx)) return false;
        std::uint32_t nfn = 0;
        if (!ReadU32(d, len, pos, nfn)) return false;
        cls.field_names.resize(nfn);
        cls.field_defaults.resize(nfn);
        for (std::size_t j = 0; j < nfn; ++j) {
            if (!ReadStr(d, len, pos, cls.field_names[j])) return false;
            if (!ReadFieldDefault(d, len, pos, cls.field_defaults[j])) return false;
        }
        cls.field_name_index.clear();
        for (std::size_t j = 0; j < cls.field_names.size(); ++j) {
            cls.field_name_index[cls.field_names[j]] = static_cast<std::uint16_t>(j);
        }
    }

    std::uint32_t n_funcs = 0;
    if (!ReadU32(d, len, pos, n_funcs)) return false;
    out_chunk->functions.resize(n_funcs);
    for (std::size_t i = 0; i < n_funcs; ++i) {
        auto &fn = out_chunk->functions[i];
        if (!ReadStr(d, len, pos, fn.name)) return false;
        if (!ReadU16(d, len, pos, fn.arity)) return false;
        if (!ReadU16(d, len, pos, fn.nlocals)) return false;
        if (!ReadU16(d, len, pos, fn.max_stack)) return false;
        if (!ReadU32(d, len, pos, fn.code_start)) return false;
        if (!ReadU32(d, len, pos, fn.code_end)) return false;
        std::uint16_t nuv = 0;
        if (!ReadU16(d, len, pos, nuv)) return false;
        fn.upvalues.resize(nuv);
        for (std::size_t j = 0; j < nuv; ++j) {
            if (!ReadU16(d, len, pos, fn.upvalues[j].source_kind)) return false;
            if (!ReadU16(d, len, pos, fn.upvalues[j].source_index)) return false;
        }
    }

    std::uint32_t n_lines = 0;
    if (!ReadU32(d, len, pos, n_lines)) return false;
    out_chunk->line_table.resize(n_lines);
    for (std::size_t i = 0; i < n_lines; ++i) {
        if (!ReadU32(d, len, pos, out_chunk->line_table[i].line)) return false;
        if (!ReadU32(d, len, pos, out_chunk->line_table[i].pc)) return false;
    }

    std::uint32_t code_size = 0;
    if (!ReadU32(d, len, pos, code_size)) return false;
    if (code_size > 0) {
        if (!ReadBytes(d, len, pos, out_chunk->code, code_size)) return false;
    }

    std::uint32_t init_size = 0;
    if (!ReadU32(d, len, pos, init_size)) return false;
    if (init_size > 0) {
        if (!ReadBytes(d, len, pos, out_chunk->init_code, init_size)) return false;
    }

    std::uint32_t n_gnames = 0;
    if (!ReadU32(d, len, pos, n_gnames)) return false;
    out_chunk->global_names.resize(n_gnames);
    for (std::size_t i = 0; i < n_gnames; ++i) {
        if (!ReadStr(d, len, pos, out_chunk->global_names[i])) return false;
    }

    if (!ReadStr(d, len, pos, out_chunk->debug_info.source_file)) return false;

    std::uint32_t n_dgn = 0;
    if (!ReadU32(d, len, pos, n_dgn)) return false;
    out_chunk->debug_info.global_names.resize(n_dgn);
    for (std::size_t i = 0; i < n_dgn; ++i) {
        if (!ReadStr(d, len, pos, out_chunk->debug_info.global_names[i])) return false;
    }

    std::uint32_t n_fdbg = 0;
    if (!ReadU32(d, len, pos, n_fdbg)) return false;
    out_chunk->debug_info.function_debug.resize(n_fdbg);
    for (std::size_t i = 0; i < n_fdbg; ++i) {
        auto &fd = out_chunk->debug_info.function_debug[i];
        std::uint16_t npn = 0;
        if (!ReadU16(d, len, pos, npn)) return false;
        fd.param_names.resize(npn);
        for (std::size_t j = 0; j < npn; ++j) {
            if (!ReadStr(d, len, pos, fd.param_names[j])) return false;
        }
        std::uint16_t nln = 0;
        if (!ReadU16(d, len, pos, nln)) return false;
        fd.local_names.resize(nln);
        for (std::size_t j = 0; j < nln; ++j) {
            if (!ReadStr(d, len, pos, fd.local_names[j])) return false;
        }
        std::uint16_t nun = 0;
        if (!ReadU16(d, len, pos, nun)) return false;
        fd.upvalue_names.resize(nun);
        for (std::size_t j = 0; j < nun; ++j) {
            if (!ReadStr(d, len, pos, fd.upvalue_names[j])) return false;
        }
    }

    out_chunk->format_version = 2;
    out_chunk->globals.assign(out_chunk->global_names.size(), Value::Nil());

    return true;
}

} // namespace vm
} // namespace lpc
