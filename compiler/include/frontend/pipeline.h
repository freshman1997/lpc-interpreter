#ifndef LPC_FRONTEND_PIPELINE_H
#define LPC_FRONTEND_PIPELINE_H

#include <string>
#include <vector>

#include "frontend/diagnostic.h"
#include "frontend/mir.h"
#include "frontend/mir_opt.h"
#include "frontend/preprocessor.h"

namespace lpc {
namespace frontend {

struct PipelineResult {
    MirModule module;
    MirModule pre_opt_module;
    bool has_pre_opt_module = false;
    int mir_instr_before_opt = 0;
    int mir_instr_after_opt = 0;
    MirOptStats mir_opt_stats;
    DiagnosticSink diagnostics;
    std::vector<SourceMapEntry> source_map;
};

struct PipelineOptions {
    bool keep_pre_opt_module = false;
};

PipelineResult CompileSourceToMir(
    const std::string &path,
    const std::string &text,
    const std::vector<std::string> &include_dirs = {},
    const PipelineOptions &options = {});

} // namespace frontend
} // namespace lpc

#endif
