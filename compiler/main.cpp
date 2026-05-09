#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include "frontend/bytecode_writer.h"
#include "frontend/pipeline.h"
#include "frontend/test_runner.h"

static void DumpMirFunction(std::ostream &os, const lpc::frontend::MirFunction &f, const std::string &title) {
    os << "fn " << f.name << " -- " << title << "\n";
    os << "  nargs=" << f.nargs << " locals=" << f.locals.size() << " upvalues=" << f.upvalues.size() << "\n";
    for (size_t i = 0; i < f.code.size(); ++i) {
        const auto &ins = f.code[i];
        os << "  [" << i << "] op=" << static_cast<int>(ins.op)
           << " a=" << ins.a << " b=" << ins.b << " line=" << ins.line << "\n";
    }
}

static void DumpMirModule(const lpc::frontend::MirModule &m, const std::string &path, const std::string &title) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "module -- " << title << "\n";
    out << "functions=" << m.functions.size() << " globals=" << m.global_variables.size() << "\n";
    for (const auto &f : m.functions) {
        DumpMirFunction(out, f, title);
    }
    DumpMirFunction(out, m.init_function, "init");
    out.close();
}

struct CompileOptions {
    std::string workspace_root;
    std::string out_root;
    std::vector<std::string> include_dirs;
    bool dump_mir_before = false;
    bool dump_mir_after = false;
};

static bool is_lpc_source(const std::filesystem::path &path) {
    return path.extension() == ".lpc";
}

static bool collect_lpc_sources(const std::filesystem::path &root, std::vector<std::string> *out, std::string *err) {
    if (!out) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        if (err) *err = "path does not exist: " + root.string();
        return false;
    }
    if (std::filesystem::is_regular_file(root, ec)) {
        if (is_lpc_source(root)) {
            out->push_back(root.string());
        } else if (err) {
            *err = "source file is not .lpc: " + root.string();
            return false;
        }
        return true;
    }
    if (!std::filesystem::is_directory(root, ec)) {
        if (err) *err = "path is neither file nor directory: " + root.string();
        return false;
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            if (err) *err = "error scanning directory: " + ec.message();
            return false;
        }
        if (entry.is_regular_file(ec) && is_lpc_source(entry.path())) {
            out->push_back(entry.path().string());
        }
    }
    std::sort(out->begin(), out->end());
    return true;
}

static int compile_one(const std::string &path, const CompileOptions &opt) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        std::cout << "failed to open: " << path << "\n";
        return 2;
    }

    std::stringstream buf;
    buf << in.rdbuf();
    lpc::frontend::PipelineOptions popt;
    popt.keep_pre_opt_module = opt.dump_mir_before;
    lpc::frontend::PipelineResult result = lpc::frontend::CompileSourceToMir(path, buf.str(), opt.include_dirs, popt);

    const auto &diags = result.diagnostics.All();
    for (const auto &d : diags) {
        const char *lvl = d.level == lpc::frontend::DiagnosticLevel::Error ? "error" :
                          d.level == lpc::frontend::DiagnosticLevel::Warning ? "warning" : "note";
        std::cout << path << ":" << lvl << "(" << d.span.line << ":" << d.span.column << "): " << d.message << "\n";
    }

    if (opt.dump_mir_before && result.has_pre_opt_module) {
        DumpMirModule(result.pre_opt_module, path + ".mir.before.txt", "before-opt");
    }
    if (opt.dump_mir_after) {
        DumpMirModule(result.module, path + ".mir.after.txt", "after-opt");
    }

    if (result.diagnostics.HasErrors()) {
        return 3;
    }

    std::string out_err;
    std::string out_module;
    if (!lpc::frontend::WriteMirModuleSetAsNextVmBytecodeAtPath(result.module, path, opt.workspace_root, opt.out_root, &out_err, &out_module, result.source_map)) {
        std::cout << path << ":error: bytecode lowering failed: " << out_err << "\n";
        return 4;
    }

    std::cout << "frontend ok: " << path << ", functions=" << result.module.functions.size() << "\n";
    std::cout << "  nextvm module " << out_module << "\n";
    std::cout << "  mir-opt instr: " << result.mir_instr_before_opt << " -> " << result.mir_instr_after_opt << "\n";
    std::cout << "  mir-opt pass: "
              << "constprop=" << result.mir_opt_stats.constprop_changed
              << "(d=" << result.mir_opt_stats.constprop_instr_delta << ") "
              << "constfold=" << result.mir_opt_stats.constfold_changed
              << "(d=" << result.mir_opt_stats.constfold_instr_delta << ") "
              << "peephole=" << result.mir_opt_stats.peephole_changed
              << "(d=" << result.mir_opt_stats.peephole_instr_delta << ") "
              << "unreachable=" << result.mir_opt_stats.unreachable_changed
              << "(d=" << result.mir_opt_stats.unreachable_instr_delta << ")\n";
    std::cout << "  module " << out_module << "\n";
    for (const auto &f : result.module.functions) {
        std::cout << "  fn " << f.name
                  << " locals=" << f.locals.size()
                  << " upvalues=" << f.upvalues.size()
                  << " mir=" << f.code.size()
                  << "\n";
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "usage: lpc_compiler <source-file-or-directory> [more files/directories ...]\n";
        std::cout << "   or: lpc_compiler --dir <directory> [--dir directory2 ...]\n";
        std::cout << "   or: lpc_compiler --test\n";
        return 1;
    }

    if (std::string(argv[1]) == "--test") {
        int failed = lpc::frontend::RunGoldenTests();
        if (failed == 0) {
            std::cout << "frontend golden tests: PASS\n";
            return 0;
        }
        std::cout << "frontend golden tests: FAIL count=" << failed << "\n";
        return 5;
    }

    CompileOptions opt;
    opt.workspace_root = std::filesystem::current_path().string();
    opt.out_root = (std::filesystem::path(opt.workspace_root) / "bin").string();
    bool workspace_root_explicit = false;
    bool out_root_explicit = false;

    std::vector<std::string> raw_inputs;
    std::vector<std::string> dir_inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-I" || arg == "--include-dir") && i + 1 < argc) {
            opt.include_dirs.push_back(argv[++i]);
            continue;
        }
        if (arg == "--out-root" && i + 1 < argc) {
            opt.out_root = argv[++i];
            out_root_explicit = true;
            continue;
        }
        if (arg == "--workspace-root" && i + 1 < argc) {
            opt.workspace_root = argv[++i];
            workspace_root_explicit = true;
            continue;
        }
        if (arg == "--dir" && i + 1 < argc) {
            dir_inputs.push_back(argv[++i]);
            continue;
        }
        if (arg == "--dump-mir-before") {
            opt.dump_mir_before = true;
            continue;
        }
        if (arg == "--dump-mir-after") {
            opt.dump_mir_after = true;
            continue;
        }
        raw_inputs.push_back(arg);
    }

    std::vector<std::string> inputs;
    std::vector<std::filesystem::path> directory_roots;
    try {
        for (const auto &in : raw_inputs) {
            std::filesystem::path p(in);
            std::error_code ec;
            if (std::filesystem::is_directory(p, ec)) {
                directory_roots.push_back(p);
            }
        }
        for (const auto &dir : dir_inputs) {
            directory_roots.push_back(std::filesystem::path(dir));
        }

        if (!workspace_root_explicit && directory_roots.size() == 1 && raw_inputs.size() + dir_inputs.size() == 1) {
            opt.workspace_root = std::filesystem::weakly_canonical(directory_roots[0]).string();
            if (!out_root_explicit) {
                opt.out_root = (std::filesystem::path(opt.workspace_root) / "bin").string();
            }
        }

        for (const auto &in : raw_inputs) {
            std::string err;
            if (!collect_lpc_sources(std::filesystem::path(in), &inputs, &err)) {
                std::cout << "error: " << err << "\n";
                return 1;
            }
        }
        for (const auto &dir : dir_inputs) {
            std::string err;
            if (!collect_lpc_sources(std::filesystem::path(dir), &inputs, &err)) {
                std::cout << "error: " << err << "\n";
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "error scanning input: " << e.what() << "\n";
        return 1;
    }

    if (opt.include_dirs.empty()) {
        opt.include_dirs.push_back(opt.workspace_root);
    }

    if (inputs.empty()) {
        std::cout << "no input files\n";
        return 1;
    }
    std::sort(inputs.begin(), inputs.end());
    inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

    int rc = 0;
    for (const auto &in : inputs) {
        int one = compile_one(in, opt);
        if (one != 0) {
            rc = one;
        }
    }
    return rc;
}
