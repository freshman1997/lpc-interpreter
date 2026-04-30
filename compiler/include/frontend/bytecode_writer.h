#ifndef LPC_FRONTEND_BYTECODE_WRITER_H
#define LPC_FRONTEND_BYTECODE_WRITER_H

#include <string>
#include <vector>

#include "frontend/mir.h"
#include "frontend/preprocessor.h"

namespace lpc {
namespace frontend {

bool WriteMirAsNextVmBytecode(const MirModule &module, const std::string &module_name, const std::string &out_path, std::string *error, const std::vector<SourceMapEntry> &source_map = {});
bool WriteMirModuleSetAsNextVmBytecodeAtPath(
    const MirModule &module,
    const std::string &source_path,
    const std::string &workspace_root,
    const std::string &out_root,
    std::string *error,
    std::string *out_module_name = nullptr,
    const std::vector<SourceMapEntry> &source_map = {});

} // namespace frontend
} // namespace lpc

#endif
