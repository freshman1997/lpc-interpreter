#include "runtime/compare_matrix.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#include "runtime/vm2_bridge.h"

namespace lpc {
namespace runtime {

core::Status RunCompareMatrix() {
    const std::vector<std::string> modules = {
        "compiler/tests/golden/sample_vm2_bridge_min",
        "compiler/tests/golden/sample_vm2_compare_ok",
        "compiler/tests/golden/sample_vm2_compare_eq",
        "compiler/tests/golden/sample_vm2_compare_neq",
        "compiler/tests/golden/sample_vm2_compare_lte",
        "compiler/tests/golden/sample_vm2_compare_gte_lt",
        "compiler/tests/golden/sample_vm2_compare_not_or",
        "compiler/tests/golden/sample_vm2_compare_logic_ops",
        "compiler/tests/golden/sample_vm2_compare_logic_call",
        "compiler/tests/golden/sample_vm2_compare_mul_div_mod",
        "compiler/tests/golden/sample_vm2_compare_class_field",
        "compiler/tests/golden/sample_vm2_compare_array_index",
    };

    int pass = 0;
    int fail = 0;
    for (const auto &m : modules) {
        std::cout << "[compare-matrix] running: " << m << std::endl;

        std::string prev_limit;
        const char *env_limit = std::getenv("LPC_VM_MAX_MEM");
        if (env_limit) {
            prev_limit = env_limit;
        }
        _putenv_s("LPC_VM_MAX_MEM", "536870912");

        core::Status s = RunEntryModuleCompare(m);

        if (env_limit) {
            _putenv_s("LPC_VM_MAX_MEM", prev_limit.c_str());
        } else {
            _putenv_s("LPC_VM_MAX_MEM", "");
        }

        if (s.ok()) {
            ++pass;
            std::cout << "[compare-matrix] PASS: " << m << std::endl;
        } else {
            ++fail;
            std::cout << "[compare-matrix] FAIL: " << m << " reason=" << s.message << std::endl;
        }
    }

    std::cout << "[compare-matrix] result pass=" << pass << " fail=" << fail << std::endl;
    if (fail > 0) {
        return core::Status::Error(core::ErrorCode::VmError, "compare matrix has failures");
    }
    return core::Status::OkStatus();
}

} // namespace runtime
} // namespace lpc
