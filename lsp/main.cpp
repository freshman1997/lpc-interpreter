#include "lsp/lsp_server.h"
#include "lpc/runtime/exit_code.h"
#include <iostream>

int main() {
    lpc::lsp::LspServer server([](const std::string &msg) {
        std::cout << msg << std::flush;
    });
    server.Run(std::cin);
    if (server.ExitCode() == 0) {
        return lpc::runtime::ToProcessCode(lpc::runtime::ExitCode::Ok);
    }
    return lpc::runtime::ToProcessCode(lpc::runtime::ExitCode::ProtocolError);
}
