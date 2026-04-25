#ifndef LPC_FRONTEND_BYTECODE_WRITER_H
#define LPC_FRONTEND_BYTECODE_WRITER_H

#include <string>

#include "frontend/mir.h"

namespace lpc {
namespace frontend {

bool WriteMirAsV1Bytecode(const MirModule &module, const std::string &module_name, const std::string &out_path, std::string *error);
bool WriteMirAsNextVmBytecode(const MirModule &module, const std::string &module_name, const std::string &out_path, std::string *error);
bool WriteMirModuleSetAsV1Bytecode(const MirModule &module, const std::string &out_dir, std::string *error);
bool WriteMirModuleSetAsV1BytecodeAtPath(
    const MirModule &module,
    const std::string &source_path,
    const std::string &workspace_root,
    const std::string &out_root,
    std::string *error,
    std::string *out_module_name = nullptr);
bool WriteMirModuleSetAsNextVmBytecodeAtPath(
    const MirModule &module,
    const std::string &source_path,
    const std::string &workspace_root,
    const std::string &out_root,
    std::string *error,
    std::string *out_module_name = nullptr);

} // namespace frontend
} // namespace lpc

#endif
