#ifndef LPC_FRONTEND2_MIR_OPT_H
#define LPC_FRONTEND2_MIR_OPT_H

#include "frontend2/mir.h"

namespace lpc {
namespace frontend2 {

struct MirOptStats {
    int function_count = 0;
    int init_count = 0;
    int before_instr = 0;
    int after_instr = 0;
    int constprop_changed = 0;
    int constfold_changed = 0;
    int peephole_changed = 0;
    int unreachable_changed = 0;
    int constprop_instr_delta = 0;
    int constfold_instr_delta = 0;
    int peephole_instr_delta = 0;
    int unreachable_instr_delta = 0;
};

void OptimizeMirModule(MirModule *module);
MirOptStats OptimizeMirModuleWithStats(MirModule *module);

} // namespace frontend2
} // namespace lpc

#endif
