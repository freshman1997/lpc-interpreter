#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <vector>
#include <sstream>

#include "cli/cli.h"
#include "core/status.h"
#include "runtime/vm.h"
#include "runtime/bytecode_translator.h"
#include "runtime/compare_matrix.h"

extern std::string get_cwd();

namespace lpc {
namespace cli {

static void SetEnvVar(const std::string &k, const std::string &v) {
#if WIN32
    _putenv_s(k.c_str(), v.c_str());
#else
    setenv(k.c_str(), v.c_str(), 1);
#endif
}

static void PrintUsage() {
    std::cout << "lpc <command> [args]\n";
    std::cout << "commands: run, compare, compare-matrix, debug, profile, gc-bench\n";
    std::cout << "run args: [entry-module] [--entry-file path] [--vm legacy|next] [--max-mem bytes] [--nursery-mem bytes]\n";
    std::cout << "debug args: [entry-module] [--entry-file path] [--script path]\n";
    std::cout << "profile args: [entry-module] [--entry-file path]\n";
    std::cout << "gc-bench args: [entry-module] [--entry-file path] [--nursery-list a,b,c] [--max-mem bytes]\n";
}

static std::vector<luint64_t> ParseNurseryList(const std::string &text) {
    std::vector<luint64_t> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        char *end = nullptr;
        unsigned long long v = std::strtoull(item.c_str(), &end, 10);
        if (end != item.c_str() && v > 0) {
            out.push_back(static_cast<luint64_t>(v));
        }
    }
    return out;
}

static void PrintGcStats(lpc_vm_t *vm, const std::string &tag) {
    std::cout << "[gc:" << tag << "] major=" << vm->gc_major_collect_count()
              << " minor=" << vm->gc_minor_collect_count()
              << " allocated=" << vm->allocated_bytes()
              << " nursery=" << vm->gc_nursery_bytes() << "/" << vm->gc_nursery_limit_bytes()
              << " remembered=" << vm->gc_remembered_set_size()
              << " wb=" << vm->gc_write_barrier_count()
              << " major_freed=" << vm->gc_major_total_freed_bytes()
              << " minor_freed=" << vm->gc_minor_total_freed_bytes()
              << " major_us=" << vm->gc_major_total_elapsed_us()
              << " minor_us=" << vm->gc_minor_total_elapsed_us()
              << std::endl;
}

static std::string ReadEntryFile(const std::string &p) {
    std::ifstream in(p.c_str(), std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::string m;
    std::getline(in, m);
    in.close();
    return m;
}

int Run(int argc, char **argv) {
    if (argc <= 1) {
        PrintUsage();
        return 0;
    }

    const std::string cmd = argv[1];
    std::string entry_file = get_cwd() + "/bin/entry.txt";
    std::string entry_arg;
    std::string vm_engine = "legacy";
    std::string debug_script;
    luint64_t max_mem = 0;
    luint64_t nursery_mem = 0;
    std::string nursery_list_raw;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--vm" && i + 1 < argc) {
            vm_engine = argv[++i];
            continue;
        }
        if (a == "--entry-file" && i + 1 < argc) {
            entry_file = argv[++i];
            continue;
        }
        if (a == "--script" && i + 1 < argc) {
            debug_script = argv[++i];
            continue;
        }
        if (a == "--max-mem" && i + 1 < argc) {
            char *end = nullptr;
            unsigned long long v = std::strtoull(argv[++i], &end, 10);
            if (end != argv[i] && v > 0) {
                max_mem = static_cast<luint64_t>(v);
            }
            continue;
        }
        if (a == "--nursery-mem" && i + 1 < argc) {
            char *end = nullptr;
            unsigned long long v = std::strtoull(argv[++i], &end, 10);
            if (end != argv[i] && v > 0) {
                nursery_mem = static_cast<luint64_t>(v);
            }
            continue;
        }
        if (a == "--nursery-list" && i + 1 < argc) {
            nursery_list_raw = argv[++i];
            continue;
        }
        if (entry_arg.empty()) {
            entry_arg = a;
        }
    }

    if (cmd == "run") {
        std::string entry_module = entry_arg;
        if (entry_module.empty()) {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                entry_module = m;
            }
        }

        if (vm_engine == "next") {
            if (entry_module.empty()) {
                return 1;
            }
            core::Status s = runtime::RunEntryModuleNextVM(entry_module);
            if (!s.ok()) {
                std::cerr << "NextVM run failed: " << s.message << std::endl;
                return 1;
            }
            return 0;
        }

        lpc_vm_t *vm = lpc_vm_t::create_vm();
        if (!entry_module.empty()) {
            vm->set_entry(entry_module.c_str());
        }
        if (max_mem > 0) {
            vm->set_memory_limit_bytes(max_mem);
        }
        if (nursery_mem > 0) {
            vm->set_nursery_limit_bytes(nursery_mem);
        }
        vm->bootstrap();
        vm->run_main();
        PrintGcStats(vm, "run");
        return 0;
    }
    if (cmd == "compare") {
        std::string entry_module = entry_arg;
        if (entry_module.empty()) {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                entry_module = m;
            }
        }
        if (entry_module.empty()) {
            return 1;
        }

        core::Status s = runtime::RunEntryModuleCompare(entry_module);
        if (!s.ok()) {
            std::cerr << "compare failed: " << s.message << std::endl;
            return 1;
        }
        return 0;
    }
    if (cmd == "compare-matrix") {
        core::Status s = runtime::RunCompareMatrix();
        if (!s.ok()) {
            std::cerr << "compare-matrix failed: " << s.message << std::endl;
            return 1;
        }
        return 0;
    }
    if (cmd == "debug") {
        lpc_vm_t *vm = lpc_vm_t::create_vm();
        if (!entry_arg.empty()) {
            vm->set_entry(entry_arg.c_str());
        } else {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                vm->set_entry(m.c_str());
            }
        }
        if (!debug_script.empty()) {
            SetEnvVar("LPC_DEBUG_SCRIPT", debug_script);
        }
        vm->bootstrap();
        vm->on_debug_mode();
        vm->start_debug();
        return 0;
    }
    if (cmd == "profile") {
        lpc_vm_t *vm = lpc_vm_t::create_vm();
        if (!entry_arg.empty()) {
            vm->set_entry(entry_arg.c_str());
        } else {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                vm->set_entry(m.c_str());
            }
        }
        vm->enable_profiler(true);
        vm->bootstrap();
        vm->run_main();
        std::string out_path = get_cwd() + "/bin/profile.json";
        std::ofstream out(out_path.c_str(), std::ios::binary);
        out << vm->dump_profile();
        out.close();
        return 0;
    }

    if (cmd == "gc-bench") {
        std::string entry_module = entry_arg;
        if (entry_module.empty()) {
            std::string m = ReadEntryFile(entry_file);
            if (!m.empty()) {
                entry_module = m;
            }
        }
        if (entry_module.empty()) {
            std::cerr << "gc-bench needs entry module" << std::endl;
            return 1;
        }

        std::vector<luint64_t> nursery_list = ParseNurseryList(nursery_list_raw);
        if (nursery_list.empty()) {
            nursery_list.push_back(1024ULL * 1024ULL);
            nursery_list.push_back(8ULL * 1024ULL * 1024ULL);
            nursery_list.push_back(64ULL * 1024ULL * 1024ULL);
        }

        for (luint64_t nsz : nursery_list) {
            lpc_vm_t *vm = lpc_vm_t::create_vm();
            vm->set_entry(entry_module.c_str());
            vm->set_non_fatal_mode(true);
            SetEnvVar("LPC_VM_NURSERY_MEM", std::to_string(nsz));
            if (max_mem > 0) {
                SetEnvVar("LPC_VM_MAX_MEM", std::to_string(max_mem));
            }
            if (max_mem > 0) {
                vm->set_memory_limit_bytes(max_mem);
            }
            vm->set_nursery_limit_bytes(nsz);
            vm->bootstrap();
            vm->run_main();
            if (vm->has_error()) {
                std::cerr << "[gc-bench] run error for nursery=" << nsz << ": "
                          << vm->last_error() << std::endl;
            }
            PrintGcStats(vm, std::string("bench-") + std::to_string(nsz));
        }
        return 0;
    }

    PrintUsage();
    return 1;
}

} // namespace cli
} // namespace lpc
