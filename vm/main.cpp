#include <iostream>
#include <cstdint>
#include <filesystem>
#include <string>

#if WIN32
#include <windows.h>
#endif

#include "os/os.h"
#include "lpc/runtime/exit_code.h"
#include "vm/runtime/process_context.h"
#include "vm/runtime/self_check.h"
#include "vm/runtime/entry.h"
#include "cli/cli.h"

static std::string cwd;

std::string get_cwd() {
    return cwd;
}

int main(int argc, char **argv)
{
	os::register_exception_handler();

    std::error_code ec;
    const std::filesystem::path now = std::filesystem::current_path(ec);
    if (ec) {
        std::cerr << "current_path error: " << ec.message() << std::endl;
        return lpc::runtime::ToProcessCode(lpc::runtime::ExitCode::IoError);
    }
    cwd = now.string();

#if WIN32
    SetConsoleOutputCP(65001);
#endif

    if (argc > 1) {
        if (argc == 2 && std::string(argv[1]) == "--self-check") {
            return lpc::vm::RunSelfChecks();
        }
        if (argc == 2 && std::string(argv[1]) == "--perf") {
            return lpc::vm::RunPerfBenchmarks();
        }
        if (argc == 2 && std::string(argv[1]) == "--self-check-perf") {
            int r = lpc::vm::RunSelfChecks();
            if (r != 0) return r;
            return lpc::vm::RunPerfBenchmarks();
        }
        return lpc::cli::Run(argc, argv);
    }

    std::cout << "Exited normally.\n";
    return 0;
}
