#ifndef LPC_NEXTVM_BYTECODE_CHUNK_H
#define LPC_NEXTVM_BYTECODE_CHUNK_H

#include <cstdint>
#include <string>
#include <vector>

namespace lpc {
namespace nextvm {

struct FunctionProto {
    std::string name;
    std::uint16_t arity = 0;
    std::uint16_t nlocals = 0;
    std::uint16_t max_stack = 0;
    std::uint32_t code_start = 0;
    std::uint32_t code_end = 0;
};

struct LineEntry {
    std::uint32_t line = 0;
    std::uint32_t pc = 0;
};

struct Chunk {
    std::string module_name;
    std::vector<std::uint8_t> code;
    std::vector<std::int64_t> iconst;
    std::vector<double> fconst;
    std::vector<std::string> sconst;
    std::vector<FunctionProto> functions;
    std::vector<std::uint16_t> class_field_counts;
    std::vector<LineEntry> line_table;
};

} // namespace nextvm
} // namespace lpc

#endif
