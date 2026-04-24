#include "frontend2/test_runner.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "frontend2/bytecode_writer.h"
#include "frontend2/pipeline.h"

namespace lpc {
namespace frontend2 {

struct GoldenCase {
    std::string path;
    bool expect_ok;
};

static std::string ReadAll(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int RunGoldenTests() {
    namespace fs = std::filesystem;

    const std::string ws = fs::current_path().string();
    const std::string out_root = (fs::path(ws) / "bin").string();
    const std::vector<std::string> include_dirs = {
        ws,
        (fs::path(ws) / "compiler/tests/golden").string(),
        (fs::path(ws) / "compiler/tests/golden/include").string(),
    };

    std::vector<GoldenCase> cases = {
        {"./compiler/tests/golden/sample_front2.lpc", true},
        {"./compiler/tests/golden/sample_for_front2.lpc", true},
        {"./compiler/tests/golden/sample_switch_front2.lpc", true},
        {"./compiler/tests/golden/sample_break_continue_front2.lpc", true},
        {"./compiler/tests/golden/sample_foreach_front2.lpc", true},
        {"./compiler/tests/golden/sample_class_new_front2.lpc", true},
        {"./compiler/tests/golden/sample_include_front2.lpc", true},
        {"./compiler/tests/golden/sample_preprocessor_if_front2.lpc", true},
        {"./compiler/tests/golden/sample_preprocessor_logic_front2.lpc", true},
        {"./compiler/tests/golden/sample_preprocessor_nested_front2.lpc", true},
        {"./compiler/tests/golden/sample_include_angle_front2.lpc", true},
        {"./compiler/tests/golden/sample_typed_decorate_front2.lpc", true},
        {"./compiler/tests/golden/sample_expr_precedence_front2.lpc", true},
        {"./compiler/tests/golden/sample_class_field_front2.lpc", true},
        {"./compiler/tests/golden/sample_catch_front2.lpc", true},
        {"./compiler/tests/golden/sample_tailrec_front2.lpc", true},
        {"./compiler/tests/golden/sample_stack_overflow_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_constfold_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_unreachable_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_branch_const_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_constprop_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_redundant_loadstore_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_cfg_jumpchain_front2.lpc", true},
        {"./compiler/tests/golden/sample_opt_catch_foreach_guard_front2.lpc", true},
        {"./compiler/tests/golden/invalid_continue_switch.lpc", false},
        {"./compiler/tests/golden/invalid_break_top.lpc", false},
        {"./compiler/tests/golden/invalid_continue_top.lpc", false},
        {"./compiler/tests/golden/invalid_undef_ident.lpc", false},
        {"./compiler/tests/golden/invalid_new_unknown_class.lpc", false},
        {"./compiler/tests/golden/invalid_include_angle_missing.lpc", false},
        {"./compiler/tests/golden/invalid_class_field_unknown.lpc", false},
        {"./compiler/tests/golden/modules/room/start.lpc", true},
        {"./compiler/tests/golden/modules/npc/vendor.lpc", true},
        {"./compiler/tests/golden/invalid_call_arity.lpc", true},
        {"./compiler/tests/golden/invalid_typed_init_mismatch.lpc", false},
        {"./compiler/tests/golden/invalid_typed_assign_mismatch.lpc", false},
        {"./compiler/tests/golden/invalid_typed_return_mismatch.lpc", false},
        {"./compiler/tests/golden/invalid_nonvoid_missing_return.lpc", false},
    };

    int failed = 0;
    for (const auto &tc : cases) {
        const std::string text = ReadAll(tc.path);
        if (text.empty()) {
            std::cout << "[MISS] " << tc.path << " (file missing)\n";
            ++failed;
            continue;
        }

        PipelineResult r = CompileSourceToMir(tc.path, text, include_dirs);
        bool ok = !r.diagnostics.HasErrors();
        std::string out_err;
        std::string module;
        if (ok) {
            ok = WriteMirModuleSetAsV1BytecodeAtPath(r.module, tc.path, ws, out_root, &out_err, &module);
            if (!ok) {
                std::cout << "[FAIL] " << tc.path << " lowering=" << out_err << "\n";
                ++failed;
                continue;
            }
            const std::filesystem::path p = std::filesystem::path(out_root) /
                std::filesystem::relative(std::filesystem::path(tc.path).parent_path(), std::filesystem::path(ws)) /
                (std::filesystem::path(tc.path).stem().string() + ".b");
            if (!fs::exists(p)) {
                std::cout << "[FAIL] " << tc.path << " missing output file: " << p.string() << "\n";
                ++failed;
                continue;
            }

            const std::filesystem::path ep = std::filesystem::path(out_root) / "entry.txt";
            if (!fs::exists(ep)) {
                std::cout << "[FAIL] " << tc.path << " missing entry file: " << ep.string() << "\n";
                ++failed;
                continue;
            }
            const std::string entry_text = ReadAll(ep.string());
            if (entry_text.empty()) {
                std::cout << "[FAIL] " << tc.path << " empty entry file\n";
                ++failed;
                continue;
            }
        }
        if (ok != tc.expect_ok) {
            std::cout << "[FAIL] " << tc.path << " expected_ok=" << (tc.expect_ok ? 1 : 0)
                      << " actual_ok=" << (ok ? 1 : 0) << "\n";
            ++failed;
        } else {
            std::cout << "[PASS] " << tc.path << "\n";
        }
    }

    return failed;
}

} // namespace frontend2
} // namespace lpc
