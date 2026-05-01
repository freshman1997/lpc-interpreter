#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <cctype>

#include "cli/cli.h"
#include "vm/runtime/entry.h"
#include "vm/runtime/hot_reload.h"
#include "vm/runtime/audit_log.h"
#include "lsp/lsp_server.h"

extern std::string get_cwd();

namespace lpc {
namespace cli {

static void PrintUsage() {
    std::cout << "lpc <command> [args]\n";
    std::cout << "commands: run, debug, hot-reload, lsp\n";
    std::cout << "run args: [entry-module] [entry-function] [--module name] [--function name] [--env key=value] [--bytecode-root path] [--dap-listen port] [--profile] [--release|--no-debug-checks]\n";
    std::cout << "debug args: [entry-module] [entry-function] [--module name] [--function name] [--env key=value] [--bytecode-root path] [--protocol dap|json|repl] [--profile]\n";
    std::cout << "hot-reload args: <module> [--bytecode-root path] [--check-only] [--dry-run] [--require-smoke func] [--status] [--allow-level L0|L1|L2] [--audit-log path]\n";
    std::cout << "lsp: start LSP server on stdin/stdout\n";
}

static void PrintHotReloadStatus(const lpc::vm::ModuleHotReloadStatus &status) {
    std::cout << "hot-reload status"
              << " module=" << status.module_name
              << " active_version=" << status.active_version
              << " prepared_version=" << status.prepared_version
              << std::endl;
    if (status.versions.empty()) {
        std::cout << "  versions: <empty>" << std::endl;
        return;
    }
    for (std::size_t i = 0; i < status.versions.size(); ++i) {
        const auto &v = status.versions[i];
        std::cout << "  - version=" << v.version_id
                  << " state=" << lpc::vm::ToString(v.state)
                  << " ref_count=" << v.ref_count
                  << " hash=" << v.chunk_hash
                  << std::endl;
    }
}

static bool ParseHotReloadLevel(const std::string &text, lpc::vm::HotReloadLevel *out_level) {
    if (!out_level) return false;
    if (text == "L0" || text == "l0") {
        *out_level = lpc::vm::HotReloadLevel::L0;
        return true;
    }
    if (text == "L1" || text == "l1") {
        *out_level = lpc::vm::HotReloadLevel::L1;
        return true;
    }
    if (text == "L2" || text == "l2") {
        *out_level = lpc::vm::HotReloadLevel::L2;
        return true;
    }
    return false;
}

static std::string ReadEntryFile(const std::string &p) {
    std::ifstream in(p.c_str(), std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::string m;
    std::getline(in, m);
    in.close();
    while (!m.empty() && std::isspace(static_cast<unsigned char>(m.back()))) {
        m.pop_back();
    }
    std::size_t start = 0;
    while (start < m.size() && std::isspace(static_cast<unsigned char>(m[start]))) {
        ++start;
    }
    if (start > 0) {
        m.erase(0, start);
    }
    return m;
}

static void AddEnvParam(std::vector<std::pair<std::string, std::string>> *env_params,
                        const std::string &text) {
    if (!env_params) return;
    std::size_t eq = text.find('=');
    if (eq == std::string::npos) {
        env_params->push_back({text, ""});
        return;
    }
    env_params->push_back({text.substr(0, eq), text.substr(eq + 1)});
}

int Run(int argc, char **argv) {
    if (argc <= 1) {
        PrintUsage();
        return 0;
    }

    const std::string cmd = argv[1];
    std::string entry_file;
    std::string bytecode_root;
    std::string entry_arg;
    std::string entry_function = "main";
    std::string protocol;
    int dap_listen_port = 0;
    bool enable_profile = false;
    bool debug_checks = true;
    std::vector<std::pair<std::string, std::string>> env_params;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--entry-file" && i + 1 < argc) {
            entry_file = argv[++i];
            continue;
        }
        if ((a == "--entry-module" || a == "--module") && i + 1 < argc) {
            entry_arg = argv[++i];
            continue;
        }
        if ((a == "--entry-function" || a == "--function") && i + 1 < argc) {
            entry_function = argv[++i];
            continue;
        }
        if ((a == "--env" || a == "--param") && i + 1 < argc) {
            AddEnvParam(&env_params, argv[++i]);
            continue;
        }
        if (a == "--bytecode-root" && i + 1 < argc) {
            bytecode_root = argv[++i];
            continue;
        }
        if (a == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
            continue;
        }
        if (a == "--dap-listen" && i + 1 < argc) {
            dap_listen_port = std::atoi(argv[++i]);
            continue;
        }
        if (a == "--profile") {
            enable_profile = true;
            continue;
        }
        if (a == "--release" || a == "--no-debug-checks") {
            debug_checks = false;
            continue;
        }
        if (a == "--debug-checks") {
            debug_checks = true;
            continue;
        }
        if (entry_arg.empty()) {
            entry_arg = a;
            continue;
        }
        if (entry_function == "main") {
            entry_function = a;
        }
    }

    if (cmd == "run") {
        if (bytecode_root.empty() && !entry_file.empty()) {
            bytecode_root = std::filesystem::path(entry_file).parent_path().string();
        }
        std::string entry_module = entry_arg;
        if (entry_module.empty()) {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                entry_module = m;
            }
        }
        if (entry_module.empty()) {
            std::cerr << "run: no entry module specified" << std::endl;
            return 1;
        }
        if (dap_listen_port > 0) {
            debug_checks = true;
        }
        lpc::vm::RuntimeError s = dap_listen_port > 0
            ? lpc::vm::RunEntryModuleAttachable(entry_module, dap_listen_port, enable_profile, bytecode_root, debug_checks, entry_function, env_params)
            : lpc::vm::RunEntryModule(entry_module, enable_profile, bytecode_root, debug_checks, entry_function, env_params);
        if (!s.ok()) {
            std::cerr << "run failed: " << s.message << std::endl;
            return 1;
        }
        return 0;
    }

    if (cmd == "debug") {
        if (bytecode_root.empty() && !entry_file.empty()) {
            bytecode_root = std::filesystem::path(entry_file).parent_path().string();
        }
        std::string entry_module = entry_arg;
        if (entry_module.empty()) {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                entry_module = m;
            }
        }
        if (entry_module.empty()) {
            std::cerr << "debug: no entry module specified" << std::endl;
            return 1;
        }
        bool protocol_json = false;
        bool protocol_dap = false;
        const char *env_proto = std::getenv("LPC_DEBUG_PROTOCOL");
        if (env_proto && std::string(env_proto) == "json") {
            protocol_json = true;
        }
        if (env_proto && std::string(env_proto) == "dap") {
            protocol_dap = true;
        }
        if (protocol == "json") {
            protocol_json = true;
        }
        if (protocol == "dap") {
            protocol_dap = true;
        }
        lpc::vm::RuntimeError s = lpc::vm::RunEntryModuleDebug(entry_module, protocol_json, protocol_dap, enable_profile, bytecode_root, entry_function, env_params);
        if (!s.ok()) {
            if (s.code != lpc::vm::RuntimeErrorCode::InternalError) {
                std::cerr << "debug failed: " << s.message << std::endl;
                return 1;
            }
        }
        return 0;
    }

    if (cmd == "hot-reload") {
        std::string module_name;
        bool check_only = false;
        bool dry_run = false;
        bool show_status = false;
        std::string smoke_function;
        lpc::vm::HotReloadLevel level = lpc::vm::HotReloadLevel::L0;
        std::string audit_log_path;

        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--check-only") {
                check_only = true;
                continue;
            }
            if (a == "--dry-run") {
                dry_run = true;
                continue;
            }
            if (a == "--status") {
                show_status = true;
                continue;
            }
            if (a == "--require-smoke" && i + 1 < argc) {
                smoke_function = argv[++i];
                continue;
            }
            if (a == "--allow-level" && i + 1 < argc) {
                if (!ParseHotReloadLevel(argv[++i], &level)) {
                    std::cerr << "hot-reload: invalid --allow-level, expected L0|L1|L2" << std::endl;
                    return 1;
                }
                continue;
            }
            if (a == "--audit-log" && i + 1 < argc) {
                audit_log_path = argv[++i];
                continue;
            }
            if (a == "--bytecode-root" && i + 1 < argc) {
                bytecode_root = argv[++i];
                continue;
            }
            if (module_name.empty()) {
                module_name = a;
            }
        }

        if (module_name.empty()) {
            std::cerr << "hot-reload: module name is required" << std::endl;
            return 1;
        }

        if (!audit_log_path.empty()) {
            lpc::vm::SetHotReloadAuditLogPath(audit_log_path);
        }

        if (show_status) {
            lpc::vm::ModuleHotReloadStatus status;
            lpc::vm::RuntimeError status_err = lpc::vm::GetHotReloadModuleStatus(module_name, &status);
            if (!status_err.ok()) {
                std::cerr << "hot-reload status failed: " << status_err.message << std::endl;
                return 1;
            }
            PrintHotReloadStatus(status);
            return 0;
        }

        lpc::vm::Chunk candidate;
        lpc::vm::RuntimeError load_err = lpc::vm::LoadModuleChunkForHotReload(module_name, &candidate, bytecode_root);
        if (!load_err.ok()) {
            std::cerr << "hot-reload load failed: " << load_err.message << std::endl;
            return 1;
        }

        if (check_only) {
            lpc::vm::HotReloadCompatReport report;
            lpc::vm::RuntimeError check_err = lpc::vm::CheckHotReloadModule(module_name, candidate, level, &report);
            if (!check_err.ok()) {
                std::cerr << "hot-reload check failed: " << check_err.message << std::endl;
                if (!report.issues.empty()) {
                    for (std::size_t i = 0; i < report.issues.size(); ++i) {
                        std::cerr << "  - " << report.issues[i].field << ": " << report.issues[i].detail << std::endl;
                    }
                }
                return 1;
            }
            std::cout << "hot-reload check passed"
                      << " module=" << module_name
                      << " level=" << lpc::vm::ToString(level)
                      << std::endl;
            return 0;
        }

        if (dry_run) {
            lpc::vm::HotReloadCompatReport report;
            lpc::vm::RuntimeError check_err = lpc::vm::CheckHotReloadModule(module_name, candidate, level, &report);
            if (!check_err.ok()) {
                std::cerr << "hot-reload dry-run failed: " << check_err.message << std::endl;
                return 1;
            }
            std::cout << "hot-reload dry-run passed"
                      << " module=" << module_name
                      << " level=" << lpc::vm::ToString(level)
                      << std::endl;
            return 0;
        }

        if (!smoke_function.empty()) {
            std::uint64_t candidate_version = 0;
            lpc::vm::ModuleHotReloadStatus prep_status;
            lpc::vm::RuntimeError smoke_err = lpc::vm::PrepareHotReloadModule(
                module_name, candidate, level, smoke_function, &candidate_version, &prep_status);
            if (!smoke_err.ok()) {
                std::cerr << "hot-reload smoke failed: " << smoke_err.message << std::endl;
                return 1;
            }

            lpc::vm::ModuleHotReloadStatus activate_status;
            lpc::vm::RuntimeError activate_err = lpc::vm::ActivatePreparedHotReloadModule(
                module_name, candidate_version, &activate_status);
            if (!activate_err.ok()) {
                std::cerr << "hot-reload activate failed after smoke: " << activate_err.message << std::endl;
                return 1;
            }

            std::cout << "hot-reload activated (smoke passed)"
                      << " module=" << module_name
                      << " active_version=" << activate_status.active_version
                      << " smoke=" << smoke_function
                      << std::endl;
            PrintHotReloadStatus(activate_status);
            return 0;
        }

        lpc::vm::ModuleHotReloadStatus status;
        lpc::vm::RuntimeError apply_err = lpc::vm::ApplyHotReloadModule(module_name, candidate, level, &status);
        if (!apply_err.ok()) {
            std::cerr << "hot-reload apply failed: " << apply_err.message << std::endl;
            return 1;
        }

        std::cout << "hot-reload activated"
                  << " module=" << module_name
                  << " active_version=" << status.active_version
                  << " prepared_version=" << status.prepared_version
                  << std::endl;
        PrintHotReloadStatus(status);
        return 0;
    }

    if (cmd == "lsp") {
        lpc::lsp::LspServer server([](const std::string &msg) {
            std::cout << msg << std::flush;
        });
        server.Run(std::cin);
        return 0;
    }

    PrintUsage();
    return 1;
}

} // namespace cli
} // namespace lpc
