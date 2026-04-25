#include "frontend/test_runner.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "frontend/bytecode_writer.h"
#include "frontend/pipeline.h"
#include "frontend/verifier.h"

namespace lpc {
namespace frontend {

struct GoldenCase {
    std::string path;
    bool expect_ok;
};

struct VerifierCase {
    std::string name;
    MirModule module;
    std::string expected_message;
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

static int RunVerifierTests() {
    std::vector<VerifierCase> cases;

    {
        VerifierCase tc;
        tc.name = "new-array-underflow";
        tc.expected_message = "array literal requires element values on stack";
        MirFunction fn;
        fn.name = "main";
        fn.code.push_back({MirOp::NewArray, 2, 0});
        fn.code.push_back({MirOp::Return, 0, 0});
        tc.module.functions.push_back(fn);
        tc.module.init_function.name = "__init_globals";
        tc.module.init_function.code.push_back({MirOp::Return, 0, 0});
        cases.push_back(tc);
    }

    {
        VerifierCase tc;
        tc.name = "call-argc-underflow";
        tc.expected_message = "call requires callee and argc values on stack";
        MirFunction fn;
        fn.name = "main";
        fn.code.push_back({MirOp::LoadFunction, 0, 0});
        fn.code.push_back({MirOp::Call, 2, 0});
        fn.code.push_back({MirOp::Return, 0, 0});
        tc.module.functions.push_back(fn);
        tc.module.init_function.name = "__init_globals";
        tc.module.init_function.code.push_back({MirOp::Return, 0, 0});
        cases.push_back(tc);
    }

    {
        VerifierCase tc;
        tc.name = "init-global-index";
        tc.expected_message = "invalid global variable index";
        MirFunction fn;
        fn.name = "main";
        fn.code.push_back({MirOp::Return, 0, 0});
        tc.module.functions.push_back(fn);
        tc.module.init_function.name = "__init_globals";
        tc.module.init_function.code.push_back({MirOp::LoadConst, 0, 0});
        tc.module.init_function.code.push_back({MirOp::StoreGlobal, 0, 0});
        tc.module.init_function.code.push_back({MirOp::Return, 0, 0});
        cases.push_back(tc);
    }

    int failed = 0;
    for (const auto &tc : cases) {
        Verify2Result r = VerifyMirModule(tc.module);
        if (r.ok || r.message != tc.expected_message) {
            std::cout << "[FAIL] verifier/" << tc.name
                      << " expected='" << tc.expected_message
                      << "' actual='" << (r.ok ? std::string("<ok>") : r.message) << "'\n";
            ++failed;
        } else {
            std::cout << "[PASS] verifier/" << tc.name << "\n";
        }
    }
    return failed;
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
        {"./compiler/tests/golden/sample.lpc", true},
        {"./compiler/tests/golden/sample_for.lpc", true},
        {"./compiler/tests/golden/sample_switch.lpc", true},
        {"./compiler/tests/golden/sample_break_continue.lpc", true},
        {"./compiler/tests/golden/sample_foreach.lpc", true},
        {"./compiler/tests/golden/sample_class_new.lpc", true},
        {"./compiler/tests/golden/sample_include.lpc", true},
        {"./compiler/tests/golden/sample_preprocessor_if.lpc", true},
        {"./compiler/tests/golden/sample_preprocessor_logic.lpc", true},
        {"./compiler/tests/golden/sample_preprocessor_nested.lpc", true},
        {"./compiler/tests/golden/sample_include_angle.lpc", true},
        {"./compiler/tests/golden/sample_typed_decorate.lpc", true},
        {"./compiler/tests/golden/sample_expr_precedence.lpc", true},
        {"./compiler/tests/golden/sample_class_field.lpc", true},
        {"./compiler/tests/golden/sample_catch.lpc", true},
        {"./compiler/tests/golden/sample_tailrec.lpc", true},
        {"./compiler/tests/golden/sample_stack_overflow.lpc", true},
        {"./compiler/tests/golden/sample_opt_constfold.lpc", true},
        {"./compiler/tests/golden/sample_opt_unreachable.lpc", true},
        {"./compiler/tests/golden/sample_opt_branch_const.lpc", true},
        {"./compiler/tests/golden/sample_opt_constprop.lpc", true},
        {"./compiler/tests/golden/sample_opt_redundant_loadstore.lpc", true},
        {"./compiler/tests/golden/sample_opt_cfg_jumpchain.lpc", true},
        {"./compiler/tests/golden/sample_opt_catch_foreach_guard.lpc", true},
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

    int failed = RunVerifierTests();
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

} // namespace frontend
} // namespace lpc
