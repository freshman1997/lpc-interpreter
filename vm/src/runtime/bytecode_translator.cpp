#include "vm/runtime/entry.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "vm/bytecode/binary_format.h"
#include "lpc/bytecode/opcode.h"
#include "vm/runtime/vm.h"
#include "vm/runtime/debugger.h"
#include "vm/value/value.h"
#include "cli/debug_repl.h"
#include "cli/dap_server.h"

extern std::string get_cwd();

namespace {

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

static bool IsRegularFile(const std::string &path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

static std::string ResolveModulePath(const std::string &entry_module, const std::string &bytecode_root = "") {
    if (entry_module.empty()) return "";
    std::filesystem::path module_path(entry_module);
    if (module_path.is_absolute() && IsRegularFile(module_path.string())) {
        return module_path.string();
    }

    std::vector<std::filesystem::path> roots;
    if (!bytecode_root.empty()) {
        roots.push_back(bytecode_root);
    }
    const std::string cwd = get_cwd();
    roots.push_back(std::filesystem::path(cwd) / "bin");
    roots.push_back(std::filesystem::path(cwd) / "build" / "compiler");
    roots.push_back(std::filesystem::path(cwd) / ".." / "build" / "compiler");

    for (const auto &root : roots) {
        std::filesystem::path candidate = root / entry_module;
        if (candidate.extension().empty()) {
            candidate.replace_extension(".nb");
        }
        if (IsRegularFile(candidate.string())) {
            return candidate.string();
        }
    }
    return "";
}

static lpc::vm::RuntimeError ParseHeaderSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    if (!ReadString(in, &out.module_name)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid header module name");
    }
    if (!ReadU32(in, &out.flags)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid header flags");
    }
    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError ParseConstantsSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    std::uint32_t count = 0;
    if (!ReadU32(in, &count)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid int const count");
    }
    out.iconst.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t bits = 0;
        if (!ReadU64(in, &bits)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid int const");
        }
        out.iconst.push_back(static_cast<std::int64_t>(bits));
    }

    if (!ReadU32(in, &count)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid float const count");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t bits = 0;
        if (!ReadU64(in, &bits)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid float const");
        }
        double v = 0.0;
        static_assert(sizeof(double) == sizeof(std::uint64_t), "double must be 64 bits");
        std::memcpy(&v, &bits, sizeof(double));
        out.fconst.push_back(v);
    }

    if (!ReadU32(in, &count)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid string const count");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string s;
        if (!ReadString(in, &s)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid string const");
        }
        out.sconst.push_back(s);
    }
    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError ParseClassesSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    std::uint32_t count = 0;
    if (!ReadU32(in, &count)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid class count");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        lpc::vm::ClassInfo ci;
        if (!ReadString(in, &ci.name)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid class name");
        }
        if (!ReadU16(in, &ci.nfields)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid class field count");
        }
        if (!ReadU16(in, &ci.parent_class_idx)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid class parent index");
        }
        for (std::uint16_t f = 0; f < ci.nfields; ++f) {
            std::string fname;
            if (!ReadString(in, &fname)) {
                return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid class field name");
            }
            ci.field_names.push_back(fname);
        }
        out.classes.push_back(std::move(ci));
    }
    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError ParseFunctionsSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    std::uint32_t count = 0;
    if (!ReadU32(in, &count)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid function count");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        lpc::vm::FunctionProto f;
        if (!ReadString(in, &f.name) ||
            !ReadU16(in, &f.arity) ||
            !ReadU16(in, &f.nlocals) ||
            !ReadU16(in, &f.max_stack) ||
            !ReadU32(in, &f.code_start) ||
            !ReadU32(in, &f.code_end)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid function entry");
        }
        std::uint16_t nupvalues = 0;
        if (!ReadU16(in, &nupvalues)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid upvalue count");
        }
        for (std::uint16_t ui = 0; ui < nupvalues; ++ui) {
            lpc::vm::UpvalueProto up;
            if (!ReadU16(in, &up.source_kind) || !ReadU16(in, &up.source_index)) {
                return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid upvalue entry");
            }
            f.upvalues.push_back(up);
        }
        out.functions.push_back(f);
    }
    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError ParseLineTableSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    std::uint32_t count = 0;
    if (!ReadU32(in, &count)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid line table count");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        lpc::vm::LineEntry e;
        if (!ReadU32(in, &e.line) || !ReadU32(in, &e.pc)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid line entry");
        }
        out.line_table.push_back(e);
    }
    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError ParseCodeSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    std::uint32_t code_size = 0;
    if (!ReadU32(in, &code_size)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid code size");
    }
    out.code.assign(code_size, 0);
    if (code_size > 0) {
        in.read(reinterpret_cast<char *>(out.code.data()), static_cast<std::streamsize>(code_size));
        if (!in.good()) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid code payload");
        }
    }

    std::uint32_t init_size = 0;
    if (!ReadU32(in, &init_size)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid init code size");
    }
    out.init_code.assign(init_size, 0);
    if (init_size > 0) {
        in.read(reinterpret_cast<char *>(out.init_code.data()), static_cast<std::streamsize>(init_size));
        if (!in.good()) {
            out.init_code.clear();
        }
    }
    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError ParseDebugSection(std::ifstream &in, std::uint32_t /*payload_size*/, lpc::vm::Chunk &out) {
    if (!ReadString(in, &out.debug_info.source_file)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug source file");
    }

    std::uint32_t n_globals = 0;
    if (!ReadU32(in, &n_globals)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug global count");
    }
    for (std::uint32_t i = 0; i < n_globals; ++i) {
        std::string name;
        if (!ReadString(in, &name)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug global name");
        }
        out.debug_info.global_names.push_back(name);
        out.global_names.push_back(name);
    }

    std::uint32_t n_func_debug = 0;
    if (!ReadU32(in, &n_func_debug)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug function count");
    }
    for (std::uint32_t i = 0; i < n_func_debug; ++i) {
        lpc::vm::FunctionDebugInfo fdi;
        std::uint16_t n_param = 0;
        if (!ReadU16(in, &n_param)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug param count");
        }
        for (std::uint16_t p = 0; p < n_param; ++p) {
            std::string name;
            if (!ReadString(in, &name)) {
                return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug param name");
            }
            fdi.param_names.push_back(name);
        }
        std::uint16_t n_local = 0;
        if (!ReadU16(in, &n_local)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug local count");
        }
        for (std::uint16_t l = 0; l < n_local; ++l) {
            std::string name;
            if (!ReadString(in, &name)) {
                return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug local name");
            }
            fdi.local_names.push_back(name);
        }
        std::uint16_t n_upval = 0;
        if (!ReadU16(in, &n_upval)) {
            return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug upvalue count");
        }
        for (std::uint16_t u = 0; u < n_upval; ++u) {
            std::string name;
            if (!ReadString(in, &name)) {
                return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid debug upvalue name");
            }
            fdi.upvalue_names.push_back(name);
        }
        out.debug_info.function_debug.push_back(std::move(fdi));
    }

    std::uint32_t n_source_map = 0;
    if (!ReadU32(in, &n_source_map)) {
        return lpc::vm::RuntimeError::Ok();
    }
    for (std::uint32_t i = 0; i < n_source_map; ++i) {
        lpc::vm::SourceMapEntry sme;
        std::uint32_t out_line = 0, src_line = 0;
        if (!ReadU32(in, &out_line) || !ReadU32(in, &src_line)) break;
        sme.output_line = static_cast<int>(out_line);
        sme.source_line = static_cast<int>(src_line);
        if (!ReadString(in, &sme.source_path)) break;
        out.debug_info.source_map.push_back(std::move(sme));
    }

    return lpc::vm::RuntimeError::Ok();
}

static lpc::vm::RuntimeError LoadChunk(const std::string &path, lpc::vm::Chunk *chunk) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.good()) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::NotFound, "bytecode file not found");
    }

    char magic[4] = {};
    in.read(magic, 4);
    if (!in.good() || std::memcmp(magic, "LPC\0", 4) != 0) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid bytecode magic");
    }

    std::uint32_t version = 0;
    if (!ReadU32(in, &version)) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "invalid bytecode version");
    }
    if (version != 2) {
        return lpc::vm::RuntimeError::Error(lpc::vm::RuntimeErrorCode::ParseError, "unsupported bytecode version");
    }

    lpc::vm::Chunk out;
    out.format_version = version;

    std::uint64_t file_pos = 8;

    while (in.good()) {
        std::uint8_t sec_id = 0;
        std::uint32_t sec_size = 0;
        if (!ReadU8(in, &sec_id)) break;
        if (!ReadU32(in, &sec_size)) break;
        file_pos += 5;

        if (sec_size == 0 && sec_id == 0) break;

        lpc::vm::RuntimeError e = lpc::vm::RuntimeError::Ok();

        switch (sec_id) {
        case lpc::vm::kSecHeader:
            e = ParseHeaderSection(in, sec_size, out);
            break;
        case lpc::vm::kSecConstants:
            e = ParseConstantsSection(in, sec_size, out);
            break;
        case lpc::vm::kSecClasses:
            e = ParseClassesSection(in, sec_size, out);
            break;
        case lpc::vm::kSecFunctions:
            e = ParseFunctionsSection(in, sec_size, out);
            break;
        case lpc::vm::kSecLineTable:
            e = ParseLineTableSection(in, sec_size, out);
            break;
        case lpc::vm::kSecCode:
            e = ParseCodeSection(in, sec_size, out);
            break;
        case lpc::vm::kSecDebug:
            e = ParseDebugSection(in, sec_size, out);
            break;
        default:
            break;
        }

        if (!e.ok()) return e;

        file_pos += sec_size;
        in.clear();
        in.seekg(static_cast<std::streamoff>(file_pos), std::ios::beg);
    }

    *chunk = std::move(out);
    return lpc::vm::RuntimeError::Ok();
}

} // namespace

namespace lpc {
namespace vm {

static vm::Vm &LiveHotReloadVm() {
    static vm::Vm live_vm;
    return live_vm;
}

RuntimeError RunEntryModule(const std::string &entry_module, bool enable_profile, const std::string &bytecode_root, bool debug_checks) {
    const std::string next_path = ResolveModulePath(entry_module, bytecode_root);
    if (next_path.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "could not find module bytecode");
    }

    vm::Chunk ch;
    RuntimeError s = LoadChunk(next_path, &ch);
    if (!s.ok()) {
        return s;
    }

    vm::Vm nextvm_engine;
    nextvm_engine.set_profile_enabled(enable_profile);
    nextvm_engine.set_debug_checks_enabled(debug_checks);
    vm::RuntimeError e = nextvm_engine.LoadChunk(ch);
    if (!e.ok()) {
        return e;
    }
    e = nextvm_engine.RunEntry("main");
    if (!e.ok()) {
        return e;
    }

    vm::Value out = nextvm_engine.last_result();
    if (out.IsInt64()) {
        std::cout << "NextVM result: " << out.AsI64() << std::endl;
    }
    if (enable_profile) {
        nextvm_engine.PrintProfile(std::cout);
    }
    return RuntimeError::Ok();
}

RuntimeError RunEntryModuleAttachable(const std::string &entry_module, int dap_listen_port, bool enable_profile, const std::string &bytecode_root, bool debug_checks) {
    const std::string next_path = ResolveModulePath(entry_module, bytecode_root);
    if (next_path.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "could not find module bytecode");
    }

    vm::Chunk ch;
    RuntimeError s = LoadChunk(next_path, &ch);
    if (!s.ok()) {
        return s;
    }

    vm::Vm nextvm_engine;
    nextvm_engine.set_profile_enabled(enable_profile);
    nextvm_engine.set_debug_checks_enabled(debug_checks);
    vm::RuntimeError e = nextvm_engine.LoadChunk(ch);
    if (!e.ok()) {
        return e;
    }

    if (!StartDapAttachServer(nextvm_engine, dap_listen_port)) {
        return RuntimeError::Error(RuntimeErrorCode::InternalError, "failed to start DAP attach server");
    }

    e = nextvm_engine.RunEntry("main");
    if (!e.ok()) {
        return e;
    }

    vm::Value out = nextvm_engine.last_result();
    if (out.IsInt64()) {
        std::cout << "NextVM result: " << out.AsI64() << std::endl;
    }
    if (enable_profile) {
        nextvm_engine.PrintProfile(std::cout);
    }
    return RuntimeError::Ok();
}

RuntimeError RunEntryModuleDebug(const std::string &entry_module, bool protocol_json, bool protocol_dap, bool enable_profile, const std::string &bytecode_root) {
    const std::string next_path = ResolveModulePath(entry_module, bytecode_root);
    if (next_path.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "could not find module bytecode");
    }

    vm::Chunk ch;
    RuntimeError s = LoadChunk(next_path, &ch);
    if (!s.ok()) {
        return s;
    }

    vm::Vm nextvm_engine;
    nextvm_engine.set_profile_enabled(enable_profile);
    vm::RuntimeError e = nextvm_engine.LoadChunk(ch);
    if (!e.ok()) {
        return e;
    }

    nextvm_engine.debugger().set_active(true);
    nextvm_engine.debugger().SetStepMode(StepMode::StepInto, 0);

    if (protocol_dap) {
        RunDapServer(nextvm_engine);
        return RuntimeError::Ok();
    }

    if (protocol_json) {
        nextvm_engine.set_debug_hook([&nextvm_engine](std::uint32_t pc) -> RuntimeError {
            return RunDapServerStep(nextvm_engine, pc);
        });
    } else {
        nextvm_engine.set_debug_hook([&nextvm_engine](std::uint32_t pc) -> RuntimeError {
            return RunDebugReplStep(nextvm_engine, pc);
        });
    }

    e = nextvm_engine.RunEntry("main");
    if (!e.ok()) {
        if (protocol_json) {
            std::cout << "{\"type\":\"event\",\"event\":\"runtimeError\",\"body\":{\"message\":"
                      << "\"" << e.message << "\"}}" << std::endl;
        } else {
            std::cerr << "runtime error: " << e.message << std::endl;
        }
        return e;
    }

    if (protocol_json) {
        std::cout << "{\"type\":\"event\",\"event\":\"terminated\",\"body\":{}}" << std::endl;
    } else {
        std::cerr << "Program exited." << std::endl;
    }

    vm::Value out = nextvm_engine.last_result();
    if (out.IsInt64() && !protocol_json) {
        std::cout << "NextVM result: " << out.AsI64() << std::endl;
    }
    if (enable_profile && !protocol_json) {
        nextvm_engine.PrintProfile(std::cout);
    }
    return RuntimeError::Ok();
}

RuntimeError LoadModuleChunkForHotReload(const std::string &module_name, Chunk *out_chunk, const std::string &bytecode_root) {
    if (!out_chunk) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "output chunk pointer is null");
    }
    const std::string path = ResolveModulePath(module_name, bytecode_root);
    if (path.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::NotFound, "could not find module bytecode");
    }
    return LoadChunk(path, out_chunk);
}

RuntimeError CheckHotReloadModule(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  HotReloadCompatReport *out_report) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload check requires module name");
    }

    vm::Vm probe;
    RuntimeError load_err = probe.LoadChunk(candidate);
    if (!load_err.ok()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand,
                                   std::string("hot-reload check failed during candidate load: ") + load_err.message);
    }

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    RuntimeError e = probe.PrepareHotReload(module_name, candidate, level, &version_id, &report);
    if (out_report) {
        *out_report = report;
    }
    if (!e.ok()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand,
                                   std::string("hot-reload compatibility rejected: ") + e.message);
    }
    return RuntimeError::Ok();
}

RuntimeError PrepareHotReloadModule(const std::string &module_name,
                                    const Chunk &candidate,
                                    HotReloadLevel level,
                                    const std::string &smoke_function,
                                    std::uint64_t *out_candidate_version,
                                    ModuleHotReloadStatus *out_status,
                                    const MigrationDescriptor *migration) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload prepare requires module name");
    }

    vm::Vm &live_vm = LiveHotReloadVm();
    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    RuntimeError e = live_vm.PrepareHotReload(module_name, candidate, level, &version_id, &report, migration);
    if (!e.ok()) {
        std::string reason = report.issues.empty() ? e.message : (report.issues[0].field + ": " + report.issues[0].detail);
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand,
                                   std::string("hot-reload prepare failed: ") + reason);
    }

    if (!smoke_function.empty()) {
        vm::Vm smoke_vm;
        RuntimeError load_err = smoke_vm.LoadChunk(candidate);
        if (!load_err.ok()) {
            return RuntimeError::Error(RuntimeErrorCode::InternalError,
                                       std::string("hot-reload smoke load failed: ") + load_err.message);
        }
        RuntimeError run_err = smoke_vm.RunEntry(smoke_function.c_str());
        if (!run_err.ok()) {
            return RuntimeError::Error(RuntimeErrorCode::InternalError,
                                       std::string("hot-reload smoke run failed: ") + run_err.message);
        }
    }

    if (out_candidate_version) {
        *out_candidate_version = version_id;
    }
    if (out_status) {
        *out_status = live_vm.GetHotReloadStatus(module_name);
    }
    return RuntimeError::Ok();
}

RuntimeError ActivatePreparedHotReloadModule(const std::string &module_name,
                                             std::uint64_t prepared_version,
                                             ModuleHotReloadStatus *out_status) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload activate requires module name");
    }
    if (prepared_version == 0) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload activate requires prepared version");
    }

    vm::Vm &live_vm = LiveHotReloadVm();
    RuntimeError e = live_vm.ActivateHotReload(module_name, prepared_version, nullptr);
    if (!e.ok()) {
        return RuntimeError::Error(RuntimeErrorCode::InternalError,
                                   std::string("hot-reload activate failed: ") + e.message);
    }
    if (out_status) {
        *out_status = live_vm.GetHotReloadStatus(module_name);
    }
    return RuntimeError::Ok();
}

RuntimeError GetHotReloadModuleStatus(const std::string &module_name,
                                      ModuleHotReloadStatus *out_status) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload status requires module name");
    }
    if (!out_status) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload status requires output pointer");
    }

    vm::Vm &live_vm = LiveHotReloadVm();
    *out_status = live_vm.GetHotReloadStatus(module_name);
    return RuntimeError::Ok();
}

RuntimeError ApplyHotReloadModule(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  ModuleHotReloadStatus *out_status) {
    if (module_name.empty()) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "hot-reload apply requires module name");
    }

    std::uint64_t candidate_version = 0;
    RuntimeError e = PrepareHotReloadModule(module_name, candidate, level, "", &candidate_version, nullptr);
    if (!e.ok()) {
        return e;
    }
    return ActivatePreparedHotReloadModule(module_name, candidate_version, out_status);
}

void SetHotReloadAuditLogPath(const std::string &file_path) {
    LiveHotReloadVm().audit_log().SetFilePath(file_path);
}

} // namespace vm
} // namespace lpc
