#ifndef LPC_VM_RUNTIME_ENTRY_H
#define LPC_VM_RUNTIME_ENTRY_H

#include <string>
#include <utility>
#include <vector>

#include "vm/runtime/error.h"
#include "vm/runtime/hot_reload.h"

namespace lpc {
namespace vm {

RuntimeError RunEntryModule(const std::string &entry_module, bool enable_profile = false, const std::string &bytecode_root = "", bool debug_checks = true, const std::string &entry_function = "main", const std::vector<std::pair<std::string, std::string>> &env_params = {});
RuntimeError RunEntryModuleAttachable(const std::string &entry_module, int dap_listen_port, bool enable_profile = false, const std::string &bytecode_root = "", bool debug_checks = true, const std::string &entry_function = "main", const std::vector<std::pair<std::string, std::string>> &env_params = {});
RuntimeError RunEntryModuleDebug(const std::string &entry_module, bool protocol_json, bool protocol_dap = false, bool enable_profile = false, const std::string &bytecode_root = "", const std::string &entry_function = "main", const std::vector<std::pair<std::string, std::string>> &env_params = {});
RuntimeError LoadModuleChunkForHotReload(const std::string &module_name, Chunk *out_chunk, const std::string &bytecode_root = "");
RuntimeError CheckHotReloadModule(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  HotReloadCompatReport *out_report);
RuntimeError PrepareHotReloadModule(const std::string &module_name,
                                    const Chunk &candidate,
                                    HotReloadLevel level,
                                    const std::string &smoke_function,
                                    std::uint64_t *out_candidate_version,
                                    ModuleHotReloadStatus *out_status,
                                    const MigrationDescriptor *migration = nullptr);
RuntimeError ActivatePreparedHotReloadModule(const std::string &module_name,
                                             std::uint64_t prepared_version,
                                             ModuleHotReloadStatus *out_status);
RuntimeError GetHotReloadModuleStatus(const std::string &module_name,
                                      ModuleHotReloadStatus *out_status);
RuntimeError ApplyHotReloadModule(const std::string &module_name,
                                  const Chunk &candidate,
                                  HotReloadLevel level,
                                  ModuleHotReloadStatus *out_status);
void SetHotReloadAuditLogPath(const std::string &file_path);

} // namespace vm
} // namespace lpc

#endif
