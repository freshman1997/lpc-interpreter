#ifndef LPC_VM_BYTECODE_CHUNK_H
#define LPC_VM_BYTECODE_CHUNK_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "vm/value/value.h"
#include "vm/runtime/vm_config.h"

namespace lpc {
namespace vm {

struct UpvalueProto {
    std::uint16_t source_kind = 0;
    std::uint16_t source_index = 0;
};

struct FunctionProto {
    std::string name;
    std::uint16_t arity = 0;
    std::uint16_t nlocals = 0;
    std::uint16_t max_stack = 0;
    std::uint32_t code_start = 0;
    std::uint32_t code_end = 0;
    std::vector<UpvalueProto> upvalues;
};

struct FunctionDebugInfo {
    std::vector<std::string> param_names;
    std::vector<std::string> local_names;
    std::vector<std::string> upvalue_names;
};

struct ClassInfo {
    struct FieldDefault {
        enum class Kind : std::uint8_t {
            Zero = 0,
            Int = 1,
            Float = 2,
            String = 3,
            Mapping = 4,
            Array = 5,
        };
        Kind kind = Kind::Zero;
        std::int64_t int_value = 0;
        double float_value = 0.0;
        std::string string_value;
        std::vector<std::pair<FieldDefault, FieldDefault>> mapping_pairs;
        std::vector<FieldDefault> array_items;
    };

    std::string name;
    std::uint16_t nfields = 0;
    std::uint16_t parent_class_idx = kInvalidIndex16;
    std::vector<std::string> field_names;
    std::vector<FieldDefault> field_defaults;
    std::unordered_map<std::string, std::uint16_t> field_name_index;
    int FindField(const std::string &name) const {
        auto it = field_name_index.find(name);
        return it != field_name_index.end() ? static_cast<int>(it->second) : -1;
    }
};

struct LineEntry {
    std::uint32_t line = 0;
    std::uint32_t pc = 0;
};

struct SourceMapEntry {
    int output_line = 0;
    int source_line = 0;
    std::string source_path;
};

struct DebugInfo {
    std::string source_file;
    std::vector<std::string> global_names;
    std::vector<FunctionDebugInfo> function_debug;
    std::vector<SourceMapEntry> source_map;
};

struct Chunk {
    std::uint32_t format_version = 0;
    std::uint32_t flags = 0;
    std::string module_name;
    std::string source_file;
    std::vector<std::uint8_t> code;
    std::vector<std::int64_t> iconst;
    std::vector<double> fconst;
    std::vector<std::string> sconst;
    std::vector<FunctionProto> functions;
    std::vector<ClassInfo> classes;
    std::vector<LineEntry> line_table;
    std::vector<std::uint8_t> init_code;
    mutable std::vector<Value> globals;
    DebugInfo debug_info;
    std::vector<std::string> global_names;
    std::uint16_t create_idx = kInvalidIndex16;
    std::uint16_t on_loadin_idx = kInvalidIndex16;
    std::uint16_t on_destruct_idx = kInvalidIndex16;
};

} // namespace vm
} // namespace lpc

#endif
