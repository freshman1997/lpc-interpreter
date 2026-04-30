#include "lsp/lsp_server.h"
#include <iostream>

int main() {
    lpc::lsp::LspServer server([](const std::string &msg) {
        std::cout << msg << std::flush;
    });
    server.Run(std::cin);
    return 0;
}
