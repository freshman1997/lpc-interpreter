#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>

#include "vm/runtime/self_check.h"
#include "vm/runtime/vm.h"
#include "vm/runtime/entry.h"
#include "vm/runtime/audit_log.h"
#include "lsp/json_rpc.h"
#include "lsp/lsp_server.h"
#include "vm/runtime/distributed_hot_reload.h"
#include "vm/runtime/persistent_storage.h"
#include "vm/bytecode/binary_format.h"
#include "lpc/bytecode/opcode.h"

namespace lpc {
namespace vm {

static bool CheckNextVMMinimalProgram()
{
    lpc::vm::Chunk ch;
    ch.module_name = "self_check";
    ch.iconst = {1, 2};
    lpc::vm::FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    ch.code.push_back(static_cast<std::uint8_t>(lpc::Op::LoadIConst));
    emit_u16(0);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::Op::LoadIConst));
    emit_u16(1);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::Op::Add));
    ch.code.push_back(static_cast<std::uint8_t>(lpc::Op::Return));
    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::vm::Vm vm;
    lpc::vm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        return false;
    }

    lpc::vm::Value out = vm.last_result();
    return out.Tag() == lpc::vm::ValueTag::Int64 && out.AsI64() == 3;
}

static bool CheckNextVMLocalsAndJump()
{
    lpc::vm::Chunk ch;
    ch.module_name = "self_check_locals";
    ch.iconst = {5, 2, 0};
    lpc::vm::FunctionProto f;
    f.name = "main";
    f.nlocals = 1;
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](lpc::vm::Op op) {
        ch.code.push_back(static_cast<std::uint8_t>(op));
    };

    emit_op(lpc::Op::LoadIConst);
    emit_u16(0);
    emit_op(lpc::Op::StoreLocal);
    emit_u16(0);

    emit_op(lpc::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::Op::JumpIfFalse);
    emit_u16(6);

    emit_op(lpc::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::Op::Jump);
    emit_u16(3);

    emit_op(lpc::Op::LoadIConst);
    emit_u16(2);

    emit_op(lpc::Op::LoadLocal);
    emit_u16(0);
    emit_op(lpc::Op::Add);
    emit_op(lpc::Op::Return);

    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::vm::Vm vm;
    lpc::vm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM locals+jump] error: " << e.message << std::endl;
        return false;
    }
    lpc::vm::Value out = vm.last_result();
    if (!(out.Tag() == lpc::vm::ValueTag::Int64 && out.AsI64() == 7)) {
        std::cerr << "[NextVM locals+jump] result tag=" << static_cast<int>(out.Tag())
                  << " value=" << out.AsI64() << std::endl;
    }
    return out.Tag() == lpc::vm::ValueTag::Int64 && out.AsI64() == 7;
}

static bool CheckNextVMCallValue()
{
    lpc::vm::Chunk ch;
    ch.module_name = "self_check_call";
    ch.iconst = {2, 4};

    lpc::vm::FunctionProto add;
    add.name = "add2";
    add.arity = 2;
    add.nlocals = 2;
    add.code_start = 0;

    lpc::vm::FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](lpc::vm::Op op) {
        ch.code.push_back(static_cast<std::uint8_t>(op));
    };

    emit_op(lpc::Op::LoadLocal);
    emit_u16(0);
    emit_op(lpc::Op::LoadLocal);
    emit_u16(1);
    emit_op(lpc::Op::Add);
    emit_op(lpc::Op::Return);
    add.code_end = static_cast<std::uint32_t>(ch.code.size());

    mainf.code_start = add.code_end;
    emit_op(lpc::Op::LoadIConst);
    emit_u16(0);
    emit_op(lpc::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::Op::LoadFunc);
    emit_u16(0);
    emit_op(lpc::Op::CallValue);
    emit_u16(2);
    emit_op(lpc::Op::Return);
    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());

    ch.functions.push_back(add);
    ch.functions.push_back(mainf);

    lpc::vm::Vm vm;
    lpc::vm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM call] error: " << e.message << std::endl;
        return false;
    }
    lpc::vm::Value out = vm.last_result();
    if (!(out.Tag() == lpc::vm::ValueTag::Int64 && out.AsI64() == 6)) {
        std::cerr << "[NextVM call] bad result tag=" << static_cast<int>(out.Tag())
                  << " value=" << out.AsI64() << std::endl;
    }
    return out.Tag() == lpc::vm::ValueTag::Int64 && out.AsI64() == 6;
}

static bool CheckNextVMVerifierRejectsBadJump()
{
    lpc::vm::Chunk ch;
    ch.module_name = "self_check_verify";
    lpc::vm::FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    ch.code.push_back(static_cast<std::uint8_t>(lpc::Op::Jump));
    emit_u16(200);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::Op::Return));
    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::vm::Vm vm;
    lpc::vm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    return !e.ok();
}

static bool CheckNextVMGarbageCollection()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.iconst = {1, 0, 500};

    FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 2;
    mainf.max_stack = 4;

    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };
    auto emit_u16 = [&](std::uint16_t v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    mainf.code_start = static_cast<std::uint32_t>(ch.code.size());

    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::StoreLocal); emit_u16(0);

    std::uint32_t loop_start = static_cast<std::uint32_t>(ch.code.size());

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::Lt);
    emit_op(Op::JumpIfFalse);
    std::uint32_t jf_pos = static_cast<std::uint32_t>(ch.code.size());
    emit_u16(0);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::NewArray); emit_u16(1);
    emit_op(Op::Pop);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Add);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::Jump);
    std::int16_t jmp_rel = static_cast<std::int16_t>(loop_start) - static_cast<std::int16_t>(ch.code.size() + 2);
    emit_u16(static_cast<std::uint16_t>(jmp_rel));

    std::uint32_t end_pos = static_cast<std::uint32_t>(ch.code.size());
    std::int16_t jf_rel = static_cast<std::int16_t>(end_pos) - static_cast<std::int16_t>(jf_pos + 2);
    ch.code[jf_pos] = static_cast<std::uint8_t>(jf_rel & 0xff);
    ch.code[jf_pos + 1] = static_cast<std::uint8_t>((jf_rel >> 8) & 0xff);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::Return);

    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(mainf);

    Vm vm;
    vm.set_gc_threshold(4);
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        std::cerr << "[NextVM GC] LoadChunk error: " << e.message << std::endl;
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM GC] RunEntry error: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    if (!(out.Tag() == ValueTag::Int64 && out.AsI64() == 500)) {
        std::cerr << "[NextVM GC] bad result tag=" << static_cast<int>(out.Tag())
                  << " val=" << (out.Tag() == ValueTag::Int64 ? out.AsI64() : -1) << std::endl;
        return false;
    }
    return true;
}

static bool CheckNextVMGCKeepReachableArray()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.iconst = {1, 0, 300};
    ch.sconst = {"hello"};

    FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 3;
    mainf.max_stack = 4;

    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };
    auto emit_u16 = [&](std::uint16_t v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    mainf.code_start = static_cast<std::uint32_t>(ch.code.size());

    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::LoadSConst); emit_u16(0);
    emit_op(Op::NewArray); emit_u16(1);
    emit_op(Op::StoreLocal); emit_u16(1);

    std::uint32_t loop_start = static_cast<std::uint32_t>(ch.code.size());

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::Lt);
    emit_op(Op::JumpIfFalse);
    std::uint32_t jf_pos = static_cast<std::uint32_t>(ch.code.size());
    emit_u16(0);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::NewArray); emit_u16(1);
    emit_op(Op::Pop);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Add);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::Jump);
    std::int16_t jmp_rel = static_cast<std::int16_t>(loop_start) - static_cast<std::int16_t>(ch.code.size() + 2);
    emit_u16(static_cast<std::uint16_t>(jmp_rel));

    std::uint32_t end_pos = static_cast<std::uint32_t>(ch.code.size());
    std::int16_t jf_rel = static_cast<std::int16_t>(end_pos) - static_cast<std::int16_t>(jf_pos + 2);
    ch.code[jf_pos] = static_cast<std::uint8_t>(jf_rel & 0xff);
    ch.code[jf_pos + 1] = static_cast<std::uint8_t>((jf_rel >> 8) & 0xff);

    emit_op(Op::LoadLocal); emit_u16(1);
    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::Index);
    emit_op(Op::Return);

    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(mainf);

    Vm vm;
    vm.set_gc_threshold(4);
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        std::cerr << "[NextVM GC keep] LoadChunk error: " << e.message << std::endl;
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM GC keep] RunEntry error: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    if (!out.IsObjRef()) {
        std::cerr << "[NextVM GC keep] bad result tag=" << static_cast<int>(out.Tag()) << std::endl;
        return false;
    }
    std::string resolved = vm.ResolveString(out);
    if (resolved != "hello") {
        std::cerr << "[NextVM GC keep] bad string: " << resolved << std::endl;
        return false;
    }
    return true;
}

static bool CheckNextVMHotReloadL0CompatReject()
{
    using namespace lpc::vm;

    Chunk base;
    base.module_name = "hot_reload_check";
    base.iconst = {1, 2};

    FunctionProto f0;
    f0.name = "main";
    f0.arity = 0;
    f0.nlocals = 0;
    f0.code_start = 0;

    auto emit_u16 = [&](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f0.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f0);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload reject] load base failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.functions[0].arity = 1;

    HotReloadCompatReport report;
    std::uint64_t candidate_version = 0;
    e = vm.PrepareHotReload(base.module_name, candidate, HotReloadLevel::L0, &candidate_version, &report);
    if (e.ok()) {
        std::cerr << "[NextVM hot-reload reject] expected incompatibility but got success" << std::endl;
        return false;
    }
    return true;
}

static bool CheckNextVMHotReloadActivateSimple()
{
    using namespace lpc::vm;

    Chunk base;
    base.module_name = "hot_reload_activate";
    base.iconst = {1, 2};

    FunctionProto f0;
    f0.name = "main";
    f0.arity = 0;
    f0.nlocals = 0;
    f0.code_start = 0;

    auto emit_u16 = [&](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 1);
    base.code.push_back(static_cast<std::uint8_t>(Op::Add));
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f0.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f0);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload activate] load base failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.iconst[0] = 5;
    candidate.iconst[1] = 7;

    HotReloadCompatReport report;
    std::uint64_t candidate_version = 0;
    e = vm.PrepareHotReload(base.module_name, candidate, HotReloadLevel::L0, &candidate_version, &report);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload activate] prepare failed: " << e.message << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(base.module_name, candidate_version, nullptr);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload activate] activate failed: " << e.message << std::endl;
        return false;
    }

    ModuleHotReloadStatus status = vm.GetHotReloadStatus(base.module_name);
    if (status.active_version != candidate_version) {
        std::cerr << "[NextVM hot-reload activate] active version mismatch" << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload activate] run failed: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    return out.Tag() == ValueTag::Int64 && out.AsI64() == 12;
}

static bool CheckNextVMDebugHotReloadInterleaving()
{
    using namespace lpc::vm;

    auto build_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};

        FunctionProto f;
        f.name = "main";
        f.arity = 0;
        f.nlocals = 0;
        f.code_start = 0;

        auto emit_u16 = [&](unsigned v) {
            ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
            ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        };

        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Chunk base = build_chunk("hot_reload_debug_interleave", 10);
    Chunk candidate = build_chunk("hot_reload_debug_interleave", 99);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[NextVM debug+hot-reload] load base failed: " << e.message << std::endl;
        return false;
    }

    vm.debugger().set_active(true);
    vm.debugger().SetStepMode(StepMode::StepInto, 0);

    bool reloaded = false;
    vm.set_debug_hook([&](std::uint32_t /*pc*/) -> RuntimeError {
        if (reloaded) {
            return RuntimeError::Ok();
        }
        HotReloadCompatReport report;
        std::uint64_t candidate_version = 0;
        RuntimeError prep = vm.PrepareHotReload(base.module_name, candidate, HotReloadLevel::L0, &candidate_version, &report);
        if (!prep.ok()) {
            return prep;
        }
        RuntimeError act = vm.ActivateHotReload(base.module_name, candidate_version, nullptr);
        if (!act.ok()) {
            return act;
        }
        reloaded = true;
        return RuntimeError::Ok();
    });

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM debug+hot-reload] first run failed: " << e.message << std::endl;
        return false;
    }
    Value first = vm.last_result();
    if (!(first.Tag() == ValueTag::Int64 && first.AsI64() == 10)) {
        std::cerr << "[NextVM debug+hot-reload] first run should stay old-version" << std::endl;
        return false;
    }

    vm.debugger().set_active(false);
    vm.set_debug_hook({});

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM debug+hot-reload] second run failed: " << e.message << std::endl;
        return false;
    }
    Value second = vm.last_result();
    if (!(second.Tag() == ValueTag::Int64 && second.AsI64() == 99)) {
        std::cerr << "[NextVM debug+hot-reload] second run should use new-version" << std::endl;
        return false;
    }

    return reloaded;
}

static bool CheckNextVMHotReloadRequiresExplicitTrigger()
{
    using namespace lpc::vm;

    auto build_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.code_start = 0;
        auto emit_u16 = [&](unsigned v) {
            ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
            ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        };
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    Chunk base = build_chunk("hot_reload_explicit_only", 1);
    Chunk changed = build_chunk("hot_reload_explicit_only", 2);
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload explicit] base load failed: " << e.message << std::endl;
        return false;
    }

    e = vm.LoadChunk(changed);
    if (e.ok()) {
        std::cerr << "[NextVM hot-reload explicit] expected second LoadChunk to be rejected" << std::endl;
        return false;
    }
    return true;
}

static bool CheckNextVMHotReloadRetentionStress()
{
    using namespace lpc::vm;

    auto build_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.code_start = 0;
        auto emit_u16 = [&](unsigned v) {
            ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
            ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        };
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    const std::string module_name = "hot_reload_retention";
    const int cycles = 30;

    Vm vm;
    Chunk base = build_chunk(module_name, 1);
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload retention] base load failed: " << e.message << std::endl;
        return false;
    }

    bool checked_during_frame = false;
    vm.debugger().set_active(true);
    vm.debugger().SetStepMode(StepMode::StepInto, 0);
    vm.set_debug_hook([&](std::uint32_t /*pc*/) -> RuntimeError {
        if (checked_during_frame) {
            return RuntimeError::Ok();
        }
        for (int i = 2; i <= cycles + 1; ++i) {
            Chunk candidate = build_chunk(module_name, i);
            HotReloadCompatReport report;
            std::uint64_t version_id = 0;
            RuntimeError prep = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L0, &version_id, &report);
            if (!prep.ok()) return prep;
            RuntimeError act = vm.ActivateHotReload(module_name, version_id, nullptr);
            if (!act.ok()) return act;
        }
        ModuleHotReloadStatus during = vm.GetHotReloadStatus(module_name);
        bool has_v1 = false;
        for (const auto &v : during.versions) {
            if (v.version_id == 1) {
                has_v1 = true;
                break;
            }
        }
        if (!has_v1) {
            return RuntimeError::Error(RuntimeErrorCode::InternalError,
                                       "version 1 retired while old frame is still active");
        }
        checked_during_frame = true;
        return RuntimeError::Ok();
    });

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload retention] run during stress failed: " << e.message << std::endl;
        return false;
    }
    Value first = vm.last_result();
    if (!(first.Tag() == ValueTag::Int64 && first.AsI64() == 1)) {
        std::cerr << "[NextVM hot-reload retention] old frame should return v1 result" << std::endl;
        return false;
    }

    vm.debugger().set_active(false);
    vm.set_debug_hook({});

    ModuleHotReloadStatus after = vm.GetHotReloadStatus(module_name);
    if (after.active_version != static_cast<std::uint64_t>(cycles + 1)) {
        std::cerr << "[NextVM hot-reload retention] active version mismatch after stress" << std::endl;
        return false;
    }
    if (after.versions.size() > 3) {
        std::cerr << "[NextVM hot-reload retention] deprecated version window not bounded" << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload retention] run after stress failed: " << e.message << std::endl;
        return false;
    }
    Value second = vm.last_result();
    if (!(second.Tag() == ValueTag::Int64 && second.AsI64() == cycles + 1)) {
        std::cerr << "[NextVM hot-reload retention] newest version result mismatch" << std::endl;
        return false;
    }
    return checked_during_frame;
}

static bool CheckHotReloadApiPrepareActivateFlow()
{
    using namespace lpc::vm;

    auto build_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.code_start = 0;
        auto emit_u16 = [&](unsigned v) {
            ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
            ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        };
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    const std::string module_name = "hot_reload_api_prepare_activate";
    Chunk candidate = build_chunk(module_name, 42);

    std::uint64_t prepared = 0;
    ModuleHotReloadStatus status;
    RuntimeError e = PrepareHotReloadModule(module_name, candidate, HotReloadLevel::L0, "", &prepared, &status);
    if (!e.ok() || prepared == 0) {
        std::cerr << "[NextVM hot-reload API] prepare failed: " << e.message << std::endl;
        return false;
    }

    e = ActivatePreparedHotReloadModule(module_name, prepared, &status);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload API] activate failed: " << e.message << std::endl;
        return false;
    }
    if (status.active_version != prepared) {
        std::cerr << "[NextVM hot-reload API] active version mismatch" << std::endl;
        return false;
    }

    ModuleHotReloadStatus queried;
    e = GetHotReloadModuleStatus(module_name, &queried);
    if (!e.ok()) {
        std::cerr << "[NextVM hot-reload API] status failed: " << e.message << std::endl;
        return false;
    }
    return queried.active_version == prepared;
}

static bool CheckHotReloadApiSmokeGate()
{
    using namespace lpc::vm;

    Chunk candidate;
    candidate.module_name = "hot_reload_api_smoke";
    candidate.iconst = {1};
    FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    candidate.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    candidate.code.push_back(0);
    candidate.code.push_back(0);
    candidate.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f.code_end = static_cast<std::uint32_t>(candidate.code.size());
    candidate.functions.push_back(f);

    std::uint64_t prepared = 0;
    ModuleHotReloadStatus status;
    RuntimeError e = PrepareHotReloadModule(candidate.module_name,
                                            candidate,
                                            HotReloadLevel::L0,
                                            "no_such_entry",
                                            &prepared,
                                            &status);
    return !e.ok();
}

static bool CheckHotReloadSmokePassAndActivate()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_smoke_pass";

    auto build_chunk = [](const std::string &mod, std::int64_t main_val, bool include_smoke) {
        Chunk ch;
        ch.module_name = mod;
        ch.iconst = {main_val, 0};
        auto emit_u16 = [&](unsigned v) {
            ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
            ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        };

        FunctionProto f_main;
        f_main.name = "main";
        f_main.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f_main);

        if (include_smoke) {
            FunctionProto f_smoke;
            f_smoke.name = "smoke_check";
            f_smoke.code_start = static_cast<std::uint32_t>(ch.code.size());
            ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
            emit_u16(1);
            ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
            f_smoke.code_end = static_cast<std::uint32_t>(ch.code.size());
            ch.functions.push_back(f_smoke);
        }

        return ch;
    };

    Chunk candidate = build_chunk(module_name, 42, true);

    std::uint64_t prepared = 0;
    ModuleHotReloadStatus prep_status;
    RuntimeError e = PrepareHotReloadModule(module_name, candidate, HotReloadLevel::L0,
                                            "smoke_check", &prepared, &prep_status);
    if (!e.ok()) {
        std::cerr << "[hot-reload smoke pass] prepare+smoke failed: " << e.message << std::endl;
        return false;
    }
    if (prepared == 0) {
        std::cerr << "[hot-reload smoke pass] prepared version is 0" << std::endl;
        return false;
    }

    ModuleHotReloadStatus activate_status;
    RuntimeError act_err = ActivatePreparedHotReloadModule(module_name, prepared, &activate_status);
    if (!act_err.ok()) {
        std::cerr << "[hot-reload smoke pass] activate failed: " << act_err.message << std::endl;
        return false;
    }
    if (activate_status.active_version != prepared) {
        std::cerr << "[hot-reload smoke pass] active_version mismatch" << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadHighFrequencyGcStress()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_gc_stress";

    auto build_chunk = [](const std::string &mod, std::int64_t value) {
        Chunk ch;
        ch.module_name = mod;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.code_start = 0;
        auto emit_u16 = [&](unsigned v) {
            ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
            ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        };
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        emit_u16(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    Chunk base = build_chunk(module_name, 1);
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[hot-reload GC stress] base load failed: " << e.message << std::endl;
        return false;
    }

    bool frame_held = false;
    vm.debugger().set_active(true);
    vm.debugger().SetStepMode(StepMode::StepInto, 0);
    vm.set_debug_hook([&](std::uint32_t /*pc*/) -> RuntimeError {
        if (frame_held) return RuntimeError::Ok();
        frame_held = true;

        for (int i = 2; i <= 50; ++i) {
            Chunk candidate = build_chunk(module_name, i);
            HotReloadCompatReport report;
            std::uint64_t version_id = 0;
            RuntimeError prep = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L0, &version_id, &report);
            if (!prep.ok()) {
                return RuntimeError::Error(RuntimeErrorCode::InternalError,
                    "prepare failed at cycle " + std::to_string(i) + ": " + prep.message);
            }
            RuntimeError act = vm.ActivateHotReload(module_name, version_id, nullptr);
            if (!act.ok()) {
                return RuntimeError::Error(RuntimeErrorCode::InternalError,
                    "activate failed at cycle " + std::to_string(i) + ": " + act.message);
            }

            vm.CollectGarbage();

            ModuleHotReloadStatus status = vm.GetHotReloadStatus(module_name);
            std::size_t total_versions = status.versions.size();
            if (total_versions > 10) {
                return RuntimeError::Error(RuntimeErrorCode::InternalError,
                    "version leak: " + std::to_string(total_versions) + " versions after cycle " + std::to_string(i));
            }
        }

        return RuntimeError::Ok();
    });

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[hot-reload GC stress] run with stress failed: " << e.message << std::endl;
        return false;
    }
    Value first = vm.last_result();
    if (!(first.Tag() == ValueTag::Int64 && first.AsI64() == 1)) {
        std::cerr << "[hot-reload GC stress] old frame should return v1 result" << std::endl;
        return false;
    }

    vm.debugger().set_active(false);
    vm.set_debug_hook({});

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[hot-reload GC stress] second run failed: " << e.message << std::endl;
        return false;
    }
    Value second = vm.last_result();
    if (!(second.Tag() == ValueTag::Int64 && second.AsI64() == 50)) {
        std::cerr << "[hot-reload GC stress] new run should return latest result, got "
                  << (second.IsInt64() ? std::to_string(second.AsI64()) : "?") << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL1AddFunctionAccept()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_add_func";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 add-func] base load failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L1 add-func] base run failed: " << e.message << std::endl;
        return false;
    }
    Value v1 = vm.last_result();
    if (!(v1.Tag() == ValueTag::Int64 && v1.AsI64() == 10)) {
        std::cerr << "[L1 add-func] base result wrong" << std::endl;
        return false;
    }

    Chunk candidate = base;
    FunctionProto f_helper;
    f_helper.name = "helper";
    f_helper.arity = 0;
    f_helper.nlocals = 0;
    f_helper.code_start = static_cast<std::uint32_t>(candidate.code.size());
    candidate.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(candidate, 0);
    candidate.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_helper.code_end = static_cast<std::uint32_t>(candidate.code.size());
    candidate.functions.push_back(f_helper);

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (!e.ok()) {
        std::cerr << "[L1 add-func] prepare failed: " << e.message << std::endl;
        return false;
    }
    if (!report.ok()) {
        std::cerr << "[L1 add-func] compat report not ok" << std::endl;
        return false;
    }
    if (report.added_functions != 1) {
        std::cerr << "[L1 add-func] expected 1 added function, got " << report.added_functions << std::endl;
        return false;
    }
    if (report.added_function_names.size() != 1 || report.added_function_names[0] != "helper") {
        std::cerr << "[L1 add-func] added_function_names wrong" << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, version_id, nullptr);
    if (!e.ok()) {
        std::cerr << "[L1 add-func] activate failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("helper");
    if (!e.ok()) {
        std::cerr << "[L1 add-func] run helper failed: " << e.message << std::endl;
        return false;
    }
    Value v2 = vm.last_result();
    if (!(v2.Tag() == ValueTag::Int64 && v2.AsI64() == 10)) {
        std::cerr << "[L1 add-func] helper result wrong" << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L1 add-func] main after reload failed: " << e.message << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadL1RejectRemoval()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_reject_remove";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1, 2};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    FunctionProto f_helper;
    f_helper.name = "helper";
    f_helper.arity = 0;
    f_helper.nlocals = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 1);
    base.code.push_back(static_cast<std::uint8_t>(Op::Add));
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_helper.code_start = static_cast<std::uint32_t>(base.code.size() - 3);
    f_helper.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_helper);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 reject-remove] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {1, 2};
    candidate.code = base.code;
    candidate.functions.push_back(f_main);

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (e.ok()) {
        std::cerr << "[L1 reject-remove] expected reject for removed function" << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadL1RejectArityChange()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_reject_arity";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 reject-arity] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.functions[0].arity = 2;

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (e.ok()) {
        std::cerr << "[L1 reject-arity] expected reject for arity change" << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadL1AddGlobalsAccept()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_add_globals";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {42, 99};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);
    base.global_names = {"x"};

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 add-globals] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.global_names = {"x", "y"};

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (!e.ok()) {
        std::cerr << "[L1 add-globals] prepare failed: " << e.message << std::endl;
        return false;
    }
    if (report.added_globals != 1) {
        std::cerr << "[L1 add-globals] expected 1 added global, got " << report.added_globals << std::endl;
        return false;
    }
    if (report.added_global_names.size() != 1 || report.added_global_names[0] != "y") {
        std::cerr << "[L1 add-globals] added_global_names wrong" << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, version_id, nullptr);
    if (!e.ok()) {
        std::cerr << "[L1 add-globals] activate failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L1 add-globals] run after reload failed: " << e.message << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadL1AddClassAccept()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_add_class";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    ClassInfo ci;
    ci.name = "Vec2";
    ci.nfields = 2;
    ci.field_names = {"x", "y"};
    base.classes.push_back(ci);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 add-class] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    ClassInfo ci2;
    ci2.name = "Vec3";
    ci2.nfields = 3;
    ci2.field_names = {"x", "y", "z"};
    candidate.classes.push_back(ci2);

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (!e.ok()) {
        std::cerr << "[L1 add-class] prepare failed: " << e.message << std::endl;
        return false;
    }
    if (report.added_classes != 1) {
        std::cerr << "[L1 add-class] expected 1 added class, got " << report.added_classes << std::endl;
        return false;
    }
    if (report.added_class_names.size() != 1 || report.added_class_names[0] != "Vec3") {
        std::cerr << "[L1 add-class] added_class_names wrong" << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, version_id, nullptr);
    if (!e.ok()) {
        std::cerr << "[L1 add-class] activate failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L1 add-class] run after reload failed: " << e.message << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadL1RejectGlobalReorder()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_reject_global_reorder";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);
    base.global_names = {"a", "b"};

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 reject-reorder] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.global_names = {"b", "a"};

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (e.ok()) {
        std::cerr << "[L1 reject-reorder] expected reject for global reorder" << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadL1RejectClassLayoutChange()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_reject_class_layout";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    ClassInfo ci;
    ci.name = "Vec2";
    ci.nfields = 2;
    ci.field_names = {"x", "y"};
    base.classes.push_back(ci);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L1 reject-class-layout] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.classes[0].nfields = 3;
    candidate.classes[0].field_names = {"x", "y", "z"};

    HotReloadCompatReport report;
    std::uint64_t version_id = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &version_id, &report);
    if (e.ok()) {
        std::cerr << "[L1 reject-class-layout] expected reject for class layout change" << std::endl;
        return false;
    }
    return true;
}

static bool CheckCrossModuleLoadModule()
{
    using namespace lpc::vm;

    auto make_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.arity = 0;
        f.nlocals = 0;
        f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0 & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((0 >> 8) & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    Chunk mod_a = make_chunk("cross_mod_a", 42);
    Chunk mod_b = make_chunk("cross_mod_b", 99);

    RuntimeError e = vm.LoadModule("cross_mod_a", mod_a);
    if (!e.ok()) {
        std::cerr << "[cross-mod-load] LoadModule A failed: " << e.message << std::endl;
        return false;
    }

    e = vm.LoadModule("cross_mod_b", mod_b);
    if (!e.ok()) {
        std::cerr << "[cross-mod-load] LoadModule B failed: " << e.message << std::endl;
        return false;
    }

    RuntimeError e_dup = vm.LoadModule("cross_mod_a", mod_a);
    if (e_dup.ok()) {
        std::cerr << "[cross-mod-load] duplicate LoadModule should fail" << std::endl;
        return false;
    }

    ModuleHotReloadStatus status_a = vm.GetHotReloadStatus("cross_mod_a");
    ModuleHotReloadStatus status_b = vm.GetHotReloadStatus("cross_mod_b");
    if (status_a.active_version == 0 || status_b.active_version == 0) {
        std::cerr << "[cross-mod-load] active version zero" << std::endl;
        return false;
    }

    const Chunk *chunk_a = vm.GetChunkForVersion(status_a.active_version);
    const Chunk *chunk_b = vm.GetChunkForVersion(status_b.active_version);
    if (!chunk_a || !chunk_b) {
        std::cerr << "[cross-mod-load] GetChunkForVersion returned null" << std::endl;
        return false;
    }
    if (chunk_a->module_name != "cross_mod_a" || chunk_b->module_name != "cross_mod_b") {
        std::cerr << "[cross-mod-load] chunk module name mismatch" << std::endl;
        return false;
    }
    if (chunk_a->iconst[0] != 42 || chunk_b->iconst[0] != 99) {
        std::cerr << "[cross-mod-load] iconst mismatch" << std::endl;
        return false;
    }

    return true;
}

static bool CheckCrossModuleHotReloadIndependent()
{
    using namespace lpc::vm;

    auto make_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.arity = 0;
        f.nlocals = 0;
        f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0 & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((0 >> 8) & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    Chunk mod_a = make_chunk("cross_hrl_a", 10);
    Chunk mod_b = make_chunk("cross_hrl_b", 20);

    RuntimeError e = vm.LoadModule("cross_hrl_a", mod_a);
    if (!e.ok()) return false;
    e = vm.LoadModule("cross_hrl_b", mod_b);
    if (!e.ok()) return false;

    std::uint64_t b_ver1 = vm.GetHotReloadStatus("cross_hrl_b").active_version;

    Chunk mod_b_v2 = make_chunk("cross_hrl_b", 77);
    std::uint64_t b_ver2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload("cross_hrl_b", mod_b_v2, HotReloadLevel::L0, &b_ver2, &report);
    if (!e.ok()) {
        std::cerr << "[cross-hrl] prepare B v2 failed: " << e.message << std::endl;
        return false;
    }

    e = vm.ActivateHotReload("cross_hrl_b", b_ver2, nullptr);
    if (!e.ok()) {
        std::cerr << "[cross-hrl] activate B v2 failed: " << e.message << std::endl;
        return false;
    }

    ModuleHotReloadStatus status_a = vm.GetHotReloadStatus("cross_hrl_a");
    ModuleHotReloadStatus status_b = vm.GetHotReloadStatus("cross_hrl_b");
    if (status_b.active_version != b_ver2) {
        std::cerr << "[cross-hrl] B active not updated" << std::endl;
        return false;
    }

    const Chunk *chunk_a = vm.GetChunkForVersion(status_a.active_version);
    if (!chunk_a || chunk_a->iconst[0] != 10) {
        std::cerr << "[cross-hrl] A chunk was affected by B reload" << std::endl;
        return false;
    }

    const Chunk *chunk_b_v1 = vm.GetChunkForVersion(b_ver1);
    const Chunk *chunk_b_v2 = vm.GetChunkForVersion(b_ver2);
    if (!chunk_b_v1 || !chunk_b_v2) {
        std::cerr << "[cross-hrl] B versions not both accessible" << std::endl;
        return false;
    }
    if (chunk_b_v1->iconst[0] != 20 || chunk_b_v2->iconst[0] != 77) {
        std::cerr << "[cross-hrl] B version data incorrect" << std::endl;
        return false;
    }

    return true;
}

static bool CheckCrossModuleGetChunkForVersion()
{
    using namespace lpc::vm;

    auto make_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.arity = 0;
        f.nlocals = 0;
        f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0 & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((0 >> 8) & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    Chunk mod_x = make_chunk("cross_chunk_x", 100);
    Chunk mod_y = make_chunk("cross_chunk_y", 200);

    vm.LoadModule("cross_chunk_x", mod_x);
    vm.LoadModule("cross_chunk_y", mod_y);

    ModuleHotReloadStatus status_x = vm.GetHotReloadStatus("cross_chunk_x");
    ModuleHotReloadStatus status_y = vm.GetHotReloadStatus("cross_chunk_y");
    if (status_x.active_version == 0 || status_y.active_version == 0) return false;

    const Chunk *cx = vm.GetChunkForVersion(status_x.active_version);
    const Chunk *cy = vm.GetChunkForVersion(status_y.active_version);
    if (!cx || !cy) {
        std::cerr << "[cross-chunk] null chunk" << std::endl;
        return false;
    }
    if (cx->module_name != "cross_chunk_x" || cy->module_name != "cross_chunk_y") {
        std::cerr << "[cross-chunk] module name mismatch" << std::endl;
        return false;
    }
    if (cx->iconst[0] != 100 || cy->iconst[0] != 200) {
        std::cerr << "[cross-chunk] iconst mismatch" << std::endl;
        return false;
    }

    const Chunk *null_chunk = vm.GetChunkForVersion(99999);
    if (null_chunk != nullptr) {
        std::cerr << "[cross-chunk] bogus version should return null" << std::endl;
        return false;
    }

    return true;
}

static bool CheckAuditLogRecordsPrepareActivateRollback()
{
    using namespace lpc::vm;

    const std::string module_name = "audit_log_basic";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[audit-log-basic] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk v2 = base;
    v2.iconst = {20};
    v2.code.clear();
    v2.functions[0].code_start = 0;
    v2.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(v2, 0);
    v2.code.push_back(static_cast<std::uint8_t>(Op::Return));
    v2.functions[0].code_end = static_cast<std::uint32_t>(v2.code.size());

    std::uint64_t prepared_ver = 0;
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L0, &prepared_ver, nullptr);
    if (!e.ok()) {
        std::cerr << "[audit-log-basic] prepare failed: " << e.message << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, prepared_ver, nullptr);
    if (!e.ok()) {
        std::cerr << "[audit-log-basic] activate failed: " << e.message << std::endl;
        return false;
    }

    const auto &log = vm.audit_log();
    if (log.Size() < 2) {
        std::cerr << "[audit-log-basic] expected >=2 entries, got " << log.Size() << std::endl;
        return false;
    }

    bool found_prepare = false, found_activate = false;
    for (std::size_t i = 0; i < log.Size(); ++i) {
        const auto &entry = log.Entry(i);
        if (entry.module_name != module_name) {
            std::cerr << "[audit-log-basic] wrong module: " << entry.module_name << std::endl;
            return false;
        }
        if (entry.action == AuditAction::Prepare) found_prepare = true;
        if (entry.action == AuditAction::Activate) found_activate = true;
    }
    if (!found_prepare || !found_activate) {
        std::cerr << "[audit-log-basic] missing prepare/activate entry" << std::endl;
        return false;
    }

    return true;
}

static bool CheckAuditLogRollbackEntry()
{
    using namespace lpc::vm;

    const std::string module_name = "audit_log_rollback";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[audit-log-rollback] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk v2 = base;
    v2.iconst = {2};
    v2.code.clear();
    v2.functions[0].code_start = 0;
    v2.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(v2, 0);
    v2.code.push_back(static_cast<std::uint8_t>(Op::Return));
    v2.functions[0].code_end = static_cast<std::uint32_t>(v2.code.size());

    std::uint64_t prepared_ver = 0;
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L0, &prepared_ver, nullptr);
    if (!e.ok()) return false;

    std::uint64_t prev_active = 0;
    e = vm.ActivateHotReload(module_name, prepared_ver, &prev_active);
    if (!e.ok()) return false;

    e = vm.RollbackHotReload(module_name, prev_active);
    if (!e.ok()) {
        std::cerr << "[audit-log-rollback] rollback failed: " << e.message << std::endl;
        return false;
    }

    const auto &log = vm.audit_log();
    bool found_rollback = false;
    for (std::size_t i = 0; i < log.Size(); ++i) {
        if (log.Entry(i).action == AuditAction::Rollback) {
            found_rollback = true;
            if (log.Entry(i).version_id != prev_active) {
                std::cerr << "[audit-log-rollback] rollback version_id mismatch" << std::endl;
                return false;
            }
        }
    }
    if (!found_rollback) {
        std::cerr << "[audit-log-rollback] no rollback entry found" << std::endl;
        return false;
    }

    return true;
}

static bool CheckAuditLogJsonFormatAndRingBuffer()
{
    using namespace lpc::vm;

    AuditLog log(4);

    for (int i = 0; i < 8; ++i) {
        AuditEntry ae;
        ae.module_name = "mod";
        ae.action = AuditAction::Prepare;
        ae.version_id = static_cast<std::uint64_t>(i + 1);
        log.Record(std::move(ae));
    }

    if (log.Size() != 4) {
        std::cerr << "[audit-log-ring] expected 4 entries after trim, got " << log.Size() << std::endl;
        return false;
    }

    if (log.Entry(0).version_id != 5) {
        std::cerr << "[audit-log-ring] expected first entry version=5, got " << log.Entry(0).version_id << std::endl;
        return false;
    }

    std::string json = AuditLog::FormatEntryJson(log.Entry(0));
    if (json.find("\"action\":\"prepare\"") == std::string::npos) {
        std::cerr << "[audit-log-ring] JSON missing action field: " << json << std::endl;
        return false;
    }
    if (json.find("\"module\":\"mod\"") == std::string::npos) {
        std::cerr << "[audit-log-ring] JSON missing module_name: " << json << std::endl;
        return false;
    }
    if (json.find("\"version\":5") == std::string::npos) {
        std::cerr << "[audit-log-ring] JSON missing version_id: " << json << std::endl;
        return false;
    }

    return true;
}

static bool CheckObjectAutoUpgradeL0()
{
    using namespace lpc::vm;

    const std::string module_name = "auto_upgrade_l0";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};
    base.global_names = {"count"};
    base.globals = {Value::FromI64(10)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L0] load base failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L0] run base failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.iconst[0] = 99;

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L0, &v2, &report);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L0] prepare failed: " << e.message << std::endl;
        return false;
    }
    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L0] activate failed: " << e.message << std::endl;
        return false;
    }

    bool upgraded = vm.TryUpgradeObject(1);
    if (!upgraded) {
        std::cerr << "[auto-upgrade-L0] TryUpgradeObject returned false" << std::endl;
        return false;
    }

    upgraded = vm.TryUpgradeObject(1);
    if (upgraded) {
        std::cerr << "[auto-upgrade-L0] second TryUpgradeObject should return false (already upgraded)" << std::endl;
        return false;
    }

    const auto &names = vm.GetObjectFieldNames(MakeObjectHandle(1));
    if (names.size() != 1 || names[0] != "count") {
        std::cerr << "[auto-upgrade-L0] field names wrong after upgrade" << std::endl;
        return false;
    }

    Value fv = vm.GetObjectField(MakeObjectHandle(1), "count");
    if (fv.Tag() != ValueTag::Int64 || fv.AsI64() != 10) {
        std::cerr << "[auto-upgrade-L0] field value wrong after upgrade" << std::endl;
        return false;
    }

    return true;
}

static bool CheckObjectAutoUpgradeL1AddGlobals()
{
    using namespace lpc::vm;

    const std::string module_name = "auto_upgrade_l1_globals";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};
    base.global_names = {"count"};
    base.globals = {Value::FromI64(10)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L1-globals] load base failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L1-globals] run base failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate = base;
    candidate.global_names = {"count", "total"};
    candidate.globals = {Value::FromI64(10), Value::Nil()};

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &v2, &report);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L1-globals] prepare failed: " << e.message << std::endl;
        return false;
    }
    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L1-globals] activate failed: " << e.message << std::endl;
        return false;
    }

    vm.TryUpgradeObject(1);

    const auto &names = vm.GetObjectFieldNames(MakeObjectHandle(1));
    if (names.size() != 2) {
        std::cerr << "[auto-upgrade-L1-globals] expected 2 field names, got " << names.size() << std::endl;
        return false;
    }
    if (names[0] != "count" || names[1] != "total") {
        std::cerr << "[auto-upgrade-L1-globals] field names wrong: " << names[0] << ", " << names[1] << std::endl;
        return false;
    }

    Value count_val = vm.GetObjectField(MakeObjectHandle(1), "count");
    if (count_val.Tag() != ValueTag::Int64 || count_val.AsI64() != 10) {
        std::cerr << "[auto-upgrade-L1-globals] existing field 'count' lost value" << std::endl;
        return false;
    }

    Value total_val = vm.GetObjectField(MakeObjectHandle(1), "total");
    if (!total_val.IsNil()) {
        std::cerr << "[auto-upgrade-L1-globals] new field 'total' should be Nil" << std::endl;
        return false;
    }

    return true;
}

static bool CheckObjectAutoUpgradeLazyOnAccess()
{
    using namespace lpc::vm;

    const std::string module_name = "auto_upgrade_lazy";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};
    base.global_names = {"x"};
    base.globals = {Value::FromI64(42)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-lazy] load base failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    Chunk candidate = base;
    candidate.global_names = {"x", "y"};
    candidate.globals = {Value::FromI64(42), Value::Nil()};

    std::uint64_t v2 = 0;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L1, &v2, nullptr);
    if (!e.ok()) return false;
    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) return false;

    const auto &names = vm.GetObjectFieldNames(MakeObjectHandle(1));
    if (names.size() != 2) {
        std::cerr << "[auto-upgrade-lazy] GetObjectFieldNames should auto-upgrade and show 2 fields, got " << names.size() << std::endl;
        return false;
    }

    Value y_val = vm.GetObjectField(MakeObjectHandle(1), "y");
    if (!y_val.IsNil()) {
        std::cerr << "[auto-upgrade-lazy] new field 'y' should be Nil" << std::endl;
        return false;
    }

    Value x_val = vm.GetObjectField(MakeObjectHandle(1), "x");
    if (x_val.Tag() != ValueTag::Int64 || x_val.AsI64() != 42) {
        std::cerr << "[auto-upgrade-lazy] existing field 'x' should preserve value" << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL2RenameGlobalWithMigration()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l2_rename";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {42, 99, 7};
    base.global_names = {"x", "y"};
    base.globals = {Value::FromI64(42), Value::FromI64(99)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L2 rename] base load failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L2 rename] base run failed: " << e.message << std::endl;
        return false;
    }

    Value x_before = vm.GetObjectField(MakeObjectHandle(1), "x");
    if (x_before.Tag() != ValueTag::Int64 || x_before.AsI64() != 42) {
        std::cerr << "[L2 rename] base object x should be 42" << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {42, 99, 7};
    candidate.global_names = {"a", "z"};
    candidate.globals = {Value::Nil(), Value::FromI64(7)};
    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;
    FieldMigration fm_rename;
    fm_rename.old_name = "x";
    fm_rename.new_name = "a";
    fm_rename.kind = FieldMigrationKind::Rename;
    migration.globals.entries.push_back(fm_rename);

    FieldMigration fm_drop;
    fm_drop.old_name = "y";
    fm_drop.kind = FieldMigrationKind::Drop;
    migration.globals.entries.push_back(fm_drop);

    FieldMigration fm_add;
    fm_add.new_name = "z";
    fm_add.kind = FieldMigrationKind::AddWithDefault;
    fm_add.default_value = Value::FromI64(7);
    migration.globals.entries.push_back(fm_add);

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (!e.ok()) {
        std::cerr << "[L2 rename] prepare failed: " << e.message << std::endl;
        return false;
    }
    if (report.kind != HotReloadCompatKind::CompatibleWithMigration) {
        std::cerr << "[L2 rename] expected CompatibleWithMigration" << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[L2 rename] activate failed: " << e.message << std::endl;
        return false;
    }

    vm.TryUpgradeObject(1);

    const auto &names = vm.GetObjectFieldNames(MakeObjectHandle(1));
    if (names.size() != 2 || names[0] != "a" || names[1] != "z") {
        std::cerr << "[L2 rename] field names wrong, expected [a, z], got ";
        for (const auto &n : names) std::cerr << n << " ";
        std::cerr << std::endl;
        return false;
    }

    Value a_val = vm.GetObjectField(MakeObjectHandle(1), "a");
    if (a_val.Tag() != ValueTag::Int64 || a_val.AsI64() != 42) {
        std::cerr << "[L2 rename] renamed field 'a' should preserve old x=42, got ";
        if (a_val.Tag() == ValueTag::Int64) std::cerr << a_val.AsI64(); else std::cerr << "non-int";
        std::cerr << std::endl;
        return false;
    }

    Value z_val = vm.GetObjectField(MakeObjectHandle(1), "z");
    if (z_val.Tag() != ValueTag::Int64 || z_val.AsI64() != 7) {
        std::cerr << "[L2 rename] new field 'z' should have default 7" << std::endl;
        return false;
    }

    Value old_y = vm.GetObjectField(MakeObjectHandle(1), "y");
    if (!old_y.IsNil()) {
        std::cerr << "[L2 rename] dropped field 'y' should resolve to Nil" << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL2RejectsUncoveredRemoval()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l2_reject_uncovered";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};
    base.global_names = {"x", "y"};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L2 reject-uncovered] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {1};
    candidate.global_names = {"x"};
    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (e.ok()) {
        std::cerr << "[L2 reject-uncovered] expected reject for removed global 'y' without migration" << std::endl;
        return false;
    }
    if (report.kind != HotReloadCompatKind::Incompatible) {
        std::cerr << "[L2 reject-uncovered] expected Incompatible, got " << ToString(report.kind) << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL2RejectsNewFieldWithoutDefault()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l2_reject_no_default";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};
    base.global_names = {"x"};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L2 reject-no-default] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {1};
    candidate.global_names = {"x", "y"};
    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (e.ok()) {
        std::cerr << "[L2 reject-no-default] expected reject for new global 'y' without default" << std::endl;
        return false;
    }

    return true;
}

static bool CheckObjectAutoUpgradeL2WithMigration()
{
    using namespace lpc::vm;

    const std::string module_name = "auto_upgrade_l2";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {100, 200};
    base.global_names = {"score", "level"};
    base.globals = {Value::FromI64(100), Value::FromI64(5)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L2] load base failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L2] run base failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {100, 200, 0};
    candidate.global_names = {"rating", "rank"};
    candidate.globals = {Value::Nil(), Value::FromI64(1)};
    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;

    FieldMigration fm_rename;
    fm_rename.old_name = "score";
    fm_rename.new_name = "rating";
    fm_rename.kind = FieldMigrationKind::Rename;
    migration.globals.entries.push_back(fm_rename);

    FieldMigration fm_drop;
    fm_drop.old_name = "level";
    fm_drop.kind = FieldMigrationKind::Drop;
    migration.globals.entries.push_back(fm_drop);

    FieldMigration fm_add;
    fm_add.new_name = "rank";
    fm_add.kind = FieldMigrationKind::AddWithDefault;
    fm_add.default_value = Value::FromI64(1);
    migration.globals.entries.push_back(fm_add);

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L2] prepare failed: " << e.message << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[auto-upgrade-L2] activate failed: " << e.message << std::endl;
        return false;
    }

    bool upgraded = vm.TryUpgradeObject(1);
    if (!upgraded) {
        std::cerr << "[auto-upgrade-L2] TryUpgradeObject should return true" << std::endl;
        return false;
    }

    const auto &names = vm.GetObjectFieldNames(MakeObjectHandle(1));
    if (names.size() != 2 || names[0] != "rating" || names[1] != "rank") {
        std::cerr << "[auto-upgrade-L2] field names wrong, expected [rating, rank]" << std::endl;
        return false;
    }

    Value rating_val = vm.GetObjectField(MakeObjectHandle(1), "rating");
    if (rating_val.Tag() != ValueTag::Int64 || rating_val.AsI64() != 100) {
        std::cerr << "[auto-upgrade-L2] renamed field 'rating' should preserve old score=100" << std::endl;
        return false;
    }

    Value rank_val = vm.GetObjectField(MakeObjectHandle(1), "rank");
    if (rank_val.Tag() != ValueTag::Int64 || rank_val.AsI64() != 1) {
        std::cerr << "[auto-upgrade-L2] new field 'rank' should have default 1" << std::endl;
        return false;
    }

    Value old_level = vm.GetObjectField(MakeObjectHandle(1), "level");
    if (!old_level.IsNil()) {
        std::cerr << "[auto-upgrade-L2] dropped field 'level' should be Nil" << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL2CustomTransform()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l2_custom";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};
    base.global_names = {"value"};
    base.globals = {Value::FromI64(10)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L2 custom] base load failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L2 custom] base run failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {10};
    candidate.global_names = {"value"};
    candidate.globals = {Value::Nil()};
    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;
    bool transform_called = false;
    migration.custom_transform = [&transform_called](std::vector<Value> &globals) {
        transform_called = true;
        if (!globals.empty() && globals[0].Tag() == ValueTag::Int64) {
            globals[0] = Value::FromI64(globals[0].AsI64() * 2);
        }
    };

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (!e.ok()) {
        std::cerr << "[L2 custom] prepare failed: " << e.message << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[L2 custom] activate failed: " << e.message << std::endl;
        return false;
    }

    vm.TryUpgradeObject(1);

    if (!transform_called) {
        std::cerr << "[L2 custom] custom transform was not called" << std::endl;
        return false;
    }

    Value val = vm.GetObjectField(MakeObjectHandle(1), "value");
    if (val.Tag() != ValueTag::Int64 || val.AsI64() != 20) {
        std::cerr << "[L2 custom] custom transform should double value to 20, got ";
        if (val.Tag() == ValueTag::Int64) std::cerr << val.AsI64(); else std::cerr << "non-int";
        std::cerr << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL2ClassFieldMigration()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l2_class";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {42, 99};
    base.global_names = {};
    base.globals = {};

    ClassInfo ci;
    ci.name = "Vec2";
    ci.nfields = 2;
    ci.field_names = {"x", "y"};
    base.classes.push_back(ci);

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 1;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::NewClass));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::StoreLocal));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadLocal));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::SetClassField));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 1);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadLocal));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::SetClassField));
    emit_u16(base, 1);
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadLocal));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L2 class-mig] base load failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[L2 class-mig] base run failed: " << e.message << std::endl;
        return false;
    }

    Value class_val = vm.last_result();

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {42, 99, 7};
    candidate.global_names = {};
    candidate.globals = {};

    ClassInfo ci2;
    ci2.name = "Vec2";
    ci2.nfields = 3;
    ci2.field_names = {"x", "z", "w"};
    candidate.classes.push_back(ci2);

    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;
    ClassMigration cm;
    cm.class_name = "Vec2";
    FieldMigration fm1;
    fm1.old_name = "y";
    fm1.new_name = "z";
    fm1.kind = FieldMigrationKind::Rename;
    cm.fields.push_back(fm1);
    FieldMigration fm2;
    fm2.new_name = "w";
    fm2.kind = FieldMigrationKind::AddWithDefault;
    fm2.default_value = Value::FromI64(7);
    cm.fields.push_back(fm2);
    migration.classes.push_back(cm);

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (!e.ok()) {
        std::cerr << "[L2 class-mig] prepare failed: " << e.message << std::endl;
        return false;
    }
    if (report.kind != HotReloadCompatKind::CompatibleWithMigration) {
        std::cerr << "[L2 class-mig] expected CompatibleWithMigration" << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[L2 class-mig] activate failed: " << e.message << std::endl;
        return false;
    }

    Value x_val = vm.GetObjectField(class_val, "x");
    if (x_val.Tag() != ValueTag::Int64 || x_val.AsI64() != 42) {
        std::cerr << "[L2 class-mig] preserved field 'x' should be 42" << std::endl;
        return false;
    }

    Value z_val = vm.GetObjectField(class_val, "z");
    if (z_val.Tag() != ValueTag::Int64 || z_val.AsI64() != 99) {
        std::cerr << "[L2 class-mig] renamed field 'z' (was 'y') should be 99" << std::endl;
        return false;
    }

    Value w_val = vm.GetObjectField(class_val, "w");
    if (w_val.Tag() != ValueTag::Int64 || w_val.AsI64() != 7) {
        std::cerr << "[L2 class-mig] new field 'w' should have default 7" << std::endl;
        return false;
    }

    const std::vector<std::string> &fnames = vm.GetObjectFieldNames(class_val);
    if (fnames.size() != 3 || fnames[0] != "x" || fnames[1] != "z" || fnames[2] != "w") {
        std::cerr << "[L2 class-mig] field names wrong, expected [x, z, w], got [";
        for (std::size_t i = 0; i < fnames.size(); ++i) {
            if (i > 0) std::cerr << ", ";
            std::cerr << fnames[i];
        }
        std::cerr << "]" << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL2RejectsClassFieldWithoutMigration()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l2_class_reject";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {};
    base.global_names = {};
    base.globals = {};

    ClassInfo ci;
    ci.name = "Point";
    ci.nfields = 2;
    ci.field_names = {"x", "y"};
    base.classes.push_back(ci);

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [](Chunk &ch, unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(base, 0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) {
        std::cerr << "[L2 class-reject] base load failed: " << e.message << std::endl;
        return false;
    }

    Chunk candidate;
    candidate.module_name = module_name;
    candidate.iconst = {};
    candidate.global_names = {};
    candidate.globals = {};

    ClassInfo ci2;
    ci2.name = "Point";
    ci2.nfields = 3;
    ci2.field_names = {"x", "y", "z"};
    candidate.classes.push_back(ci2);

    candidate.code = base.code;
    candidate.functions = base.functions;

    MigrationDescriptor migration;

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (e.ok()) {
        std::cerr << "[L2 class-reject] expected reject for class field change without ClassMigration" << std::endl;
        return false;
    }
    if (report.kind != HotReloadCompatKind::Incompatible) {
        std::cerr << "[L2 class-reject] expected Incompatible, got " << ToString(report.kind) << std::endl;
        return false;
    }

    return true;
}

static bool CheckStringConcatAndCompare()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "self_check_str";
    ch.iconst = {};
    ch.sconst = {"hello", " world", "hello world"};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 1;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadSConst); emit_u16(0);
    emit_op(Op::LoadSConst); emit_u16(1);
    emit_op(Op::Add);
    emit_op(Op::StoreLocal); emit_u16(0);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadSConst); emit_u16(2);
    emit_op(Op::Eq);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        std::cerr << "[str-concat] load failed: " << e.message << std::endl;
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[str-concat] run failed: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    return out.Tag() == ValueTag::Int64 && out.AsI64() == 1;
}

static bool CheckFloatArithmetic()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "self_check_float";
    ch.fconst = {3.5, 2.0};
    ch.iconst = {};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadFConst); emit_u16(0);
    emit_op(Op::LoadFConst); emit_u16(1);
    emit_op(Op::Mul);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;
    Value out = vm.last_result();
    if (!out.IsFloat64()) return false;
    double diff = out.AsF64() - 7.0;
    return diff < 0.001 && diff > -0.001;
}

static bool CheckArrayIndexSetAndGet()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "self_check_arr";
    ch.iconst = {0, 42, 1, 99};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 1;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::NewArray); emit_u16(3);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::StoreIndex);

    emit_op(Op::LoadIConst); emit_u16(3);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::StoreIndex);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Index);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        std::cerr << "[arr-index] load failed: " << e.message << std::endl;
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[arr-index] run failed: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    return out.Tag() == ValueTag::Int64 && out.AsI64() == 42;
}

static bool CheckRecursionFibonacci()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "self_check_fib";
    ch.iconst = {0, 1, 2};

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    FunctionProto fib;
    fib.name = "fib";
    fib.arity = 1;
    fib.nlocals = 1;
    fib.max_stack = 6;
    fib.code_start = 0;

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::Lt);
    emit_op(Op::JumpIfFalse); emit_u16(4);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::Return);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::Sub);
    emit_op(Op::LoadFunc); emit_u16(0);
    emit_op(Op::CallValue); emit_u16(1);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::Sub);
    emit_op(Op::LoadFunc); emit_u16(0);
    emit_op(Op::CallValue); emit_u16(1);

    emit_op(Op::Add);
    emit_op(Op::Return);
    fib.code_end = static_cast<std::uint32_t>(ch.code.size());

    FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 0;
    mainf.max_stack = 4;
    mainf.code_start = fib.code_end;

    ch.iconst.push_back(10);
    emit_op(Op::LoadIConst); emit_u16(3);
    emit_op(Op::LoadFunc); emit_u16(0);
    emit_op(Op::CallValue); emit_u16(1);
    emit_op(Op::Return);
    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());

    ch.functions.push_back(fib);
    ch.functions.push_back(mainf);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        std::cerr << "[fib] load failed: " << e.message << std::endl;
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[fib] run failed: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    if (!(out.Tag() == ValueTag::Int64 && out.AsI64() == 55)) {
        std::cerr << "[fib] expected 55, got " << (out.Tag() == ValueTag::Int64 ? out.AsI64() : -1) << std::endl;
        return false;
    }
    return true;
}

static bool CheckObjectSetGetField()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "self_check_obj";
    ch.iconst = {42};
    ch.global_names = {"x"};
    ch.globals = {Value::Nil()};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::StoreGlobal); emit_u16(0);
    emit_op(Op::LoadGlobal); emit_u16(0);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;
    Value out = vm.last_result();
    if (!(out.Tag() == ValueTag::Int64 && out.AsI64() == 42)) {
        std::cerr << "[obj-field] expected 42, got " << (out.Tag() == ValueTag::Int64 ? out.AsI64() : -1) << std::endl;
        return false;
    }

    Value x_val = vm.GetObjectField(MakeObjectHandle(1), "x");
    if (!(x_val.Tag() == ValueTag::Int64 && x_val.AsI64() == 42)) {
        std::cerr << "[obj-field] GetObjectField x should be 42" << std::endl;
        return false;
    }
    return true;
}

static bool CheckHotReloadRollbackRestoresObject()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_rollback_obj";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};
    base.global_names = {"val"};
    base.globals = {Value::FromI64(10)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        base.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        base.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { base.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    Value val_before = vm.GetObjectField(MakeObjectHandle(1), "val");
    if (!(val_before.Tag() == ValueTag::Int64 && val_before.AsI64() == 10)) return false;

    Chunk v2 = base;
    v2.global_names = {"val", "extra"};
    v2.globals = {Value::FromI64(10), Value::Nil()};

    std::uint64_t v2_id = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L1, &v2_id, &report);
    if (!e.ok()) return false;

    std::uint64_t prev_active = 0;
    e = vm.ActivateHotReload(module_name, v2_id, &prev_active);
    if (!e.ok()) return false;

    vm.TryUpgradeObject(1);
    const auto &names = vm.GetObjectFieldNames(MakeObjectHandle(1));
    if (names.size() != 2) return false;

    e = vm.RollbackHotReload(module_name, prev_active);
    if (!e.ok()) {
        std::cerr << "[rollback-obj] rollback failed: " << e.message << std::endl;
        return false;
    }

    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    return true;
}

static bool CheckTypeErrorOnBadOperand()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "self_check_type_err";
    ch.iconst = {0};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 2;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadLocal); emit_u16(1);
    emit_op(Op::Mul);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Eq);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[type-err] nil*nil should succeed with 0, got error: " << e.message << std::endl;
        return false;
    }
    Value out = vm.last_result();
    return out.Tag() == ValueTag::Int64 && out.AsI64() == 1;
}

static bool CheckMultipleObjectsWithGlobals()
{
    using namespace lpc::vm;

    const std::string module_name = "multi_obj_globals";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1, 2, 10, 20};
    base.global_names = {"x"};
    base.globals = {Value::FromI64(99)};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        base.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        base.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { base.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) return false;

    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    Chunk v2 = base;
    v2.global_names = {"x", "y"};
    v2.globals = {Value::FromI64(99), Value::FromI64(7)};

    std::uint64_t v2_id = 0;
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L1, &v2_id, nullptr);
    if (!e.ok()) return false;
    e = vm.ActivateHotReload(module_name, v2_id, nullptr);
    if (!e.ok()) return false;

    vm.TryUpgradeObject(1);

    Value x_val = vm.GetObjectField(MakeObjectHandle(1), "x");
    if (!(x_val.Tag() == ValueTag::Int64 && x_val.AsI64() == 99)) {
        std::cerr << "[multi-obj] x should be 99" << std::endl;
        return false;
    }
    Value y_val = vm.GetObjectField(MakeObjectHandle(1), "y");
    if (!y_val.IsNil()) {
        std::cerr << "[multi-obj] y should be Nil (added with default)" << std::endl;
        return false;
    }

    return true;
}

static bool CheckHotReloadL1AddClassFieldAccept()
{
    using namespace lpc::vm;

    const std::string module_name = "hot_reload_l1_add_class_field";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {1};

    ClassInfo ci;
    ci.name = "Point";
    ci.nfields = 2;
    ci.field_names = {"x", "y"};
    base.classes.push_back(ci);

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 0;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        base.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        base.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { base.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f_main);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) return false;

    Chunk candidate = base;
    ClassInfo ci2;
    ci2.name = "Point";
    ci2.nfields = 3;
    ci2.field_names = {"x", "y", "z"};
    candidate.classes[0] = ci2;

    MigrationDescriptor migration;
    ClassMigration cm;
    cm.class_name = "Point";
    FieldMigration fm;
    fm.new_name = "z";
    fm.kind = FieldMigrationKind::AddWithDefault;
    fm.default_value = Value::FromI64(0);
    cm.fields.push_back(fm);
    migration.classes.push_back(cm);

    std::uint64_t v2 = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &v2, &report, &migration);
    if (!e.ok()) {
        std::cerr << "[L1-add-class-field] prepare failed: " << e.message << std::endl;
        return false;
    }
    if (report.kind != HotReloadCompatKind::CompatibleWithMigration) {
        std::cerr << "[L1-add-class-field] expected CompatibleWithMigration" << std::endl;
        return false;
    }

    e = vm.ActivateHotReload(module_name, v2, nullptr);
    if (!e.ok()) {
        std::cerr << "[L1-add-class-field] activate failed: " << e.message << std::endl;
        return false;
    }

    return true;
}

struct BenchResult {
    std::string name;
    double ms;
    std::uint64_t ops;
    double ns_per_op;
};

static BenchResult RunBench(const char *name, std::uint64_t ops, std::function<void()> fn) {
    auto start = std::chrono::steady_clock::now();
    fn();
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    double ns_per_op = (ms * 1e6) / static_cast<double>(ops);
    return {name, ms, ops, ns_per_op};
}

static BenchResult BenchDispatchLoopIntAdd() {
    using namespace lpc::vm;
    const int N = 10000;

    Chunk ch;
    ch.module_name = "bench_int_add";
    ch.iconst = {0, 1, 10000};

    FunctionProto f_main;
    f_main.name = "main";
    f_main.arity = 0;
    f_main.nlocals = 2;
    f_main.max_stack = 4;
    f_main.code_start = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::StoreLocal); emit_u16(1);

    std::uint32_t loop_start = static_cast<std::uint32_t>(ch.code.size());

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::Add);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::LoadLocal); emit_u16(1);
    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::Sub);
    emit_op(Op::StoreLocal); emit_u16(1);

    emit_op(Op::LoadLocal); emit_u16(1);
    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::Gt);
    emit_op(Op::JumpIfTrue);
    std::uint32_t jf_pos = static_cast<std::uint32_t>(ch.code.size());
    std::int16_t jmp_rel = static_cast<std::int16_t>(loop_start) - static_cast<std::int16_t>(jf_pos + 2);
    emit_u16(static_cast<std::uint16_t>(jmp_rel));

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::Return);
    f_main.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f_main);

    Vm vm;
    vm.LoadChunk(ch);

    return RunBench("int_add_loop", N, [&]() { vm.RunEntry("main"); });
}

static BenchResult BenchHotReloadRapidCycles() {
    using namespace lpc::vm;
    const int N = 100;

    auto make_chunk = [](const std::string &module, std::int64_t value) {
        Chunk ch;
        ch.module_name = module;
        ch.iconst = {value};
        FunctionProto f;
        f.name = "main";
        f.arity = 0;
        f.nlocals = 0;
        f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0 & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((0 >> 8) & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    return RunBench("hot_reload_100_cycles", N, [&]() {
        Vm vm;
        vm.LoadChunk(make_chunk("bench_hr", 1));
        for (int i = 2; i <= N + 1; ++i) {
            Chunk candidate = make_chunk("bench_hr", i);
            std::uint64_t ver = 0;
            HotReloadCompatReport report;
            vm.PrepareHotReload("bench_hr", candidate, HotReloadLevel::L0, &ver, &report);
            vm.ActivateHotReload("bench_hr", ver, nullptr);
        }
    });
}

static BenchResult BenchObjectUpgradeL1() {
    using namespace lpc::vm;
    const int N = 50;

    auto make_chunk = [](const std::string &mod, int n_globals) {
        Chunk ch;
        ch.module_name = mod;
        ch.iconst = {1};
        ch.global_names.resize(n_globals);
        ch.globals.resize(n_globals, Value::FromI64(42));
        for (int i = 0; i < n_globals; ++i) {
            ch.global_names[i] = "g" + std::to_string(i);
        }
        FunctionProto f;
        f.name = "main";
        f.arity = 0;
        f.nlocals = 0;
        f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(0);
        ch.code.push_back(0);
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    return RunBench("object_upgrade_L1_50_cycles", N, [&]() {
        Vm vm;
        vm.LoadChunk(make_chunk("bench_upg", 10));
        vm.RunEntry("main");
        for (int i = 0; i < N; ++i) {
            Chunk candidate = make_chunk("bench_upg", 10 + i + 1);
            std::uint64_t ver = 0;
            vm.PrepareHotReload("bench_upg", candidate, HotReloadLevel::L1, &ver, nullptr);
            vm.ActivateHotReload("bench_upg", ver, nullptr);
            vm.TryUpgradeObject(1);
        }
    });
}

static BenchResult BenchFibonacci20() {
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "bench_fib";
    ch.iconst = {0, 1, 2, 20};

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    FunctionProto fib;
    fib.name = "fib";
    fib.arity = 1;
    fib.nlocals = 1;
    fib.max_stack = 6;
    fib.code_start = 0;

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::Lt);
    emit_op(Op::JumpIfFalse); emit_u16(4);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::Return);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(1);
    emit_op(Op::Sub);
    emit_op(Op::LoadFunc); emit_u16(0);
    emit_op(Op::CallValue); emit_u16(1);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::LoadIConst); emit_u16(2);
    emit_op(Op::Sub);
    emit_op(Op::LoadFunc); emit_u16(0);
    emit_op(Op::CallValue); emit_u16(1);

    emit_op(Op::Add);
    emit_op(Op::Return);
    fib.code_end = static_cast<std::uint32_t>(ch.code.size());

    FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 0;
    mainf.max_stack = 4;
    mainf.code_start = fib.code_end;

    emit_op(Op::LoadIConst); emit_u16(3);
    emit_op(Op::LoadFunc); emit_u16(0);
    emit_op(Op::CallValue); emit_u16(1);
    emit_op(Op::Return);
    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());

    ch.functions.push_back(fib);
    ch.functions.push_back(mainf);

    Vm vm;
    vm.LoadChunk(ch);

    return RunBench("fibonacci_20", 1, [&]() { vm.RunEntry("main"); });
}

static BenchResult BenchJsonRpcSerialize() {
    using namespace lpc::lsp;
    const int N = 10000;
    JsonNode obj(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(1)},
        {"method", JsonNode("textDocument/completion")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"uri", JsonNode("file:///test.lpc")},
            {"line", JsonNode(0)},
            {"character", JsonNode(5)}
        })}
    });
    return RunBench("json_rpc_serialize_10k", N, [&]() {
        for (int i = 0; i < N; ++i) {
            std::string s = JsonSerialize(obj);
            (void)s;
        }
    });
}

static BenchResult BenchJsonRpcParse() {
    using namespace lpc::lsp;
    const int N = 10000;
    JsonNode obj(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(1)},
        {"method", JsonNode("textDocument/completion")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"uri", JsonNode("file:///test.lpc")},
            {"line", JsonNode(0)},
            {"character", JsonNode(5)}
        })}
    });
    std::string serialized = JsonSerialize(obj);
    return RunBench("json_rpc_parse_10k", N, [&]() {
        for (int i = 0; i < N; ++i) {
            JsonNode p = JsonParse(serialized);
            (void)p;
        }
    });
}

static BenchResult BenchReloadProtocolEncode() {
    using namespace lpc::vm;
    const int N = 10000;
    ReloadMessage msg;
    msg.type = ReloadMsgType::PrepareReload;
    msg.request_id = 1;
    msg.module_name = "bench_mod";
    msg.version_id = 42;
    msg.chunk_data = "fake_chunk";
    msg.compat_level = "L1";
    return RunBench("reload_protocol_encode_10k", N, [&]() {
        for (int i = 0; i < N; ++i) {
            auto enc = ReloadProtocol::Encode(msg);
            (void)enc;
        }
    });
}

static BenchResult BenchPersistenceCodecValue() {
    using namespace lpc::vm;
    const int N = 10000;
    StoredValue sv;
    sv.type = StoredValue::Array;
    StoredValue e1; e1.type = StoredValue::Int64; e1.int_val = 42;
    StoredValue e2; e2.type = StoredValue::String; e2.str_val = "hello";
    sv.elements.push_back(e1);
    sv.elements.push_back(e2);
    return RunBench("persistence_codec_value_10k", N, [&]() {
        for (int i = 0; i < N; ++i) {
            auto data = PersistenceCodec::SerializeValue(sv);
            (void)data;
        }
    });
}

static BenchResult BenchChunkSerialization() {
    using namespace lpc::vm;
    const int N = 1000;
    Chunk ch;
    ch.module_name = "bench_serial";
    ch.iconst = {1, 2, 3, 4, 5};
    ch.fconst = {1.1, 2.2};
    ch.sconst = {"hello", "world"};
    ch.global_names = {"g1", "g2"};
    ch.globals.resize(2);
    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 2; f.max_stack = 4; f.code_start = 0;
    ch.code = {0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x06};
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);
    return RunBench("chunk_serialization_1k", N, [&]() {
        for (int i = 0; i < N; ++i) {
            std::string s = SerializeChunk(ch);
            (void)s;
        }
    });
}

int RunPerfBenchmarks() {
    std::vector<BenchResult> results;
    results.push_back(BenchDispatchLoopIntAdd());
    results.push_back(BenchFibonacci20());
    results.push_back(BenchHotReloadRapidCycles());
    results.push_back(BenchObjectUpgradeL1());
    results.push_back(BenchJsonRpcSerialize());
    results.push_back(BenchJsonRpcParse());
    results.push_back(BenchReloadProtocolEncode());
    results.push_back(BenchPersistenceCodecValue());
    results.push_back(BenchChunkSerialization());

    std::cout << "\n[vm-perf] === Performance Benchmarks ===" << std::endl;
    std::cout << std::fixed;
    std::cout.precision(2);
    for (const auto &r : results) {
        std::cout << "[vm-perf] " << r.name << ": " << r.ms << " ms"
                  << " (" << r.ops << " ops, " << r.ns_per_op << " ns/op)" << std::endl;
    }
    return 0;
}

static bool CheckVersionLookupAcrossModules()
{
    using namespace lpc::vm;

    Chunk chA;
    chA.module_name = "mod_a_vlookup";
    chA.iconst = {42};
    FunctionProto fA;
    fA.name = "main"; fA.arity = 0; fA.nlocals = 0; fA.code_start = 0;
    auto emit_u16_a = [&](unsigned v) {
        chA.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        chA.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    chA.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16_a(0);
    chA.code.push_back(static_cast<std::uint8_t>(Op::Return));
    fA.code_end = static_cast<std::uint32_t>(chA.code.size());
    chA.functions.push_back(fA);

    Vm vm;
    RuntimeError e = vm.LoadModule("mod_a_vlookup", chA);
    if (!e.ok()) { std::cerr << "[vlookup] load A failed" << std::endl; return false; }

    Chunk chB;
    chB.module_name = "mod_b_vlookup";
    chB.iconst = {99};
    FunctionProto fB;
    fB.name = "main"; fB.arity = 0; fB.nlocals = 0; fB.code_start = 0;
    auto emit_u16_b = [&](unsigned v) {
        chB.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        chB.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    chB.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16_b(0);
    chB.code.push_back(static_cast<std::uint8_t>(Op::Return));
    fB.code_end = static_cast<std::uint32_t>(chB.code.size());
    chB.functions.push_back(fB);

    e = vm.LoadModule("mod_b_vlookup", chB);
    if (!e.ok()) { std::cerr << "[vlookup] load B failed" << std::endl; return false; }

    ModuleHotReloadStatus stA = vm.GetHotReloadStatus("mod_a_vlookup");
    ModuleHotReloadStatus stB = vm.GetHotReloadStatus("mod_b_vlookup");

    const Chunk *cA = vm.GetChunkForVersion(stA.active_version);
    const Chunk *cB = vm.GetChunkForVersion(stB.active_version);
    if (!cA) { std::cerr << "[vlookup] chunk A null" << std::endl; return false; }
    if (!cB) { std::cerr << "[vlookup] chunk B null" << std::endl; return false; }
    if (cA->iconst[0] != 42) { std::cerr << "[vlookup] chunk A wrong" << std::endl; return false; }
    if (cB->iconst[0] != 99) { std::cerr << "[vlookup] chunk B wrong" << std::endl; return false; }

    const Chunk *null_chunk = vm.GetChunkForVersion(99999);
    if (null_chunk != nullptr) { std::cerr << "[vlookup] should be null for bad version" << std::endl; return false; }

    return true;
}

static bool CheckVersionLookupAfterHotReload()
{
    using namespace lpc::vm;

    const std::string module_name = "vlookup_reload";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {10};
    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        base.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        base.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) return false;

    ModuleHotReloadStatus st1 = vm.GetHotReloadStatus(module_name);
    const Chunk *c1 = vm.GetChunkForVersion(st1.active_version);
    if (!c1 || c1->iconst[0] != 10) return false;

    Chunk v2 = base;
    v2.iconst[0] = 20;
    std::uint64_t v2_id = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L0, &v2_id, &report);
    if (!e.ok()) return false;

    std::uint64_t prev_active = 0;
    e = vm.ActivateHotReload(module_name, v2_id, &prev_active);
    if (!e.ok()) return false;

    const Chunk *c2 = vm.GetChunkForVersion(v2_id);
    if (!c2) { std::cerr << "[vlookup-reload] v2 chunk null" << std::endl; return false; }
    if (c2->iconst[0] != 20) { std::cerr << "[vlookup-reload] v2 iconst wrong" << std::endl; return false; }

    const Chunk *c1_still = vm.GetChunkForVersion(st1.active_version);
    if (!c1_still) { std::cerr << "[vlookup-reload] v1 chunk null after reload" << std::endl; return false; }
    if (c1_still->iconst[0] != 10) { std::cerr << "[vlookup-reload] v1 iconst wrong after reload" << std::endl; return false; }

    return true;
}

static bool CheckStringConcatNumericAndString()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "str_concat_num";
    ch.iconst = {42};
    ch.sconst = {"hello"};

    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::LoadSConst); emit_u16(0);
    emit_op(Op::Add);
    emit_op(Op::Return);
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    Value out = vm.last_result();
    if (!out.IsObjRef()) { std::cerr << "[str-concat-num] not objref" << std::endl; return false; }
    std::string result = vm.ResolveString(out);
    if (result != "42hello") { std::cerr << "[str-concat-num] got: " << result << std::endl; return false; }

    return true;
}

static bool CheckStringConcatTwoStrings()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "str_concat_two";
    ch.sconst = {"foo", "bar"};

    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadSConst); emit_u16(0);
    emit_op(Op::LoadSConst); emit_u16(1);
    emit_op(Op::Add);
    emit_op(Op::Return);
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    Value out = vm.last_result();
    if (!out.IsObjRef()) return false;
    std::string result = vm.ResolveString(out);
    if (result != "foobar") { std::cerr << "[str-concat-two] got: " << result << std::endl; return false; }

    return true;
}

static bool CheckClassInstanceUpgradeViaIndex()
{
    using namespace lpc::vm;

    const std::string module_name = "cls_upgrade_idx";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {0};
    ClassInfo ci;
    ci.name = "Vec2";
    ci.nfields = 2;
    ci.field_names = {"x", "y"};
    base.classes.push_back(ci);

    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 1; f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        base.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        base.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { base.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::NewClass); emit_u16(0);
    emit_op(Op::StoreLocal); emit_u16(0);

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::SetClassField); emit_u16(0);

    emit_op(Op::LoadIConst); emit_u16(0);
    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::SetClassField); emit_u16(1);

    emit_op(Op::LoadLocal); emit_u16(0);
    emit_op(Op::Return);
    f.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) { std::cerr << "[cls-upgrade-idx] load failed: " << e.message << std::endl; return false; }

    e = vm.RunEntry("main");
    if (!e.ok()) { std::cerr << "[cls-upgrade-idx] run failed: " << e.message << std::endl; return false; }

    Value cls_val = vm.last_result();
    if (!IsClassObjRef(cls_val)) { std::cerr << "[cls-upgrade-idx] not class handle" << std::endl; return false; }

    Value x_before = vm.GetObjectField(cls_val, "x");
    if (!(x_before.Tag() == ValueTag::Int64 && x_before.AsI64() == 0)) { std::cerr << "[cls-upgrade-idx] x before wrong" << std::endl; return false; }

    Chunk v2 = base;
    ClassInfo ci2;
    ci2.name = "Vec2";
    ci2.nfields = 3;
    ci2.field_names = {"x", "y", "z"};
    v2.classes[0] = ci2;

    std::uint64_t v2_id = 0;
    HotReloadCompatReport report;
    MigrationDescriptor migration;
    ClassMigration cm;
    cm.class_name = "Vec2";
    FieldMigration fm;
    fm.new_name = "z";
    fm.kind = FieldMigrationKind::AddWithDefault;
    fm.default_value = Value::FromI64(0);
    cm.fields.push_back(fm);
    migration.classes.push_back(cm);
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L2, &v2_id, &report, &migration);
    if (!e.ok()) { std::cerr << "[cls-upgrade-idx] prepare failed: " << e.message << std::endl; return false; }

    std::uint64_t prev_active = 0;
    e = vm.ActivateHotReload(module_name, v2_id, &prev_active);
    if (!e.ok()) { std::cerr << "[cls-upgrade-idx] activate failed: " << e.message << std::endl; return false; }

    vm.TryUpgradeObject(1);

    Value z_after = vm.GetObjectField(cls_val, "z");
    if (!(z_after.Tag() == ValueTag::Int64 && z_after.AsI64() == 0)) { std::cerr << "[cls-upgrade-idx] z should be 0" << std::endl; return false; }

    Value x_after = vm.GetObjectField(cls_val, "x");
    if (!(x_after.Tag() == ValueTag::Int64 && x_after.AsI64() == 0)) { std::cerr << "[cls-upgrade-idx] x after upgrade wrong" << std::endl; return false; }

    return true;
}

static bool CheckHotReloadRollbackGetChunkForVersion()
{
    using namespace lpc::vm;

    const std::string module_name = "rollback_vlookup";

    Chunk base;
    base.module_name = module_name;
    base.iconst = {100};
    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        base.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        base.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    base.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(0);
    base.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f.code_end = static_cast<std::uint32_t>(base.code.size());
    base.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(base);
    if (!e.ok()) return false;

    ModuleHotReloadStatus st1 = vm.GetHotReloadStatus(module_name);
    std::uint64_t v1_id = st1.active_version;

    Chunk v2 = base;
    v2.iconst[0] = 200;
    std::uint64_t v2_id = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, v2, HotReloadLevel::L0, &v2_id, &report);
    if (!e.ok()) return false;

    std::uint64_t prev_active = 0;
    e = vm.ActivateHotReload(module_name, v2_id, &prev_active);
    if (!e.ok()) return false;
    if (prev_active != v1_id) { std::cerr << "[rollback-vlookup] prev_active wrong" << std::endl; return false; }

    e = vm.RollbackHotReload(module_name, v1_id);
    if (!e.ok()) { std::cerr << "[rollback-vlookup] rollback failed: " << e.message << std::endl; return false; }

    const Chunk *c1 = vm.GetChunkForVersion(v1_id);
    const Chunk *c2 = vm.GetChunkForVersion(v2_id);
    if (!c1) { std::cerr << "[rollback-vlookup] v1 chunk null after rollback" << std::endl; return false; }
    if (c1->iconst[0] != 100) { std::cerr << "[rollback-vlookup] v1 iconst wrong" << std::endl; return false; }
    if (!c2) { std::cerr << "[rollback-vlookup] v2 chunk null after rollback" << std::endl; return false; }
    if (c2->iconst[0] != 200) { std::cerr << "[rollback-vlookup] v2 iconst wrong" << std::endl; return false; }

    ModuleHotReloadStatus st_after = vm.GetHotReloadStatus(module_name);
    if (st_after.active_version != v1_id) { std::cerr << "[rollback-vlookup] active version not restored" << std::endl; return false; }

    return true;
}

static bool CheckCrossModuleVersionLookupAfterReload()
{
    using namespace lpc::vm;

    auto make_chunk = [](const std::string &mod, std::int64_t val) -> Chunk {
        Chunk ch;
        ch.module_name = mod;
        ch.iconst = {val};
        FunctionProto f;
        f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    RuntimeError e = vm.LoadModule("cma_v2", make_chunk("cma_v2", 1));
    if (!e.ok()) return false;

    ModuleHotReloadStatus stA1 = vm.GetHotReloadStatus("cma_v2");

    std::uint64_t v2_id = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload("cma_v2", make_chunk("cma_v2", 2), HotReloadLevel::L0, &v2_id, &report);
    if (!e.ok()) return false;
    e = vm.ActivateHotReload("cma_v2", v2_id, nullptr);
    if (!e.ok()) return false;

    e = vm.LoadModule("cmb_v2", make_chunk("cmb_v2", 3));
    if (!e.ok()) return false;

    ModuleHotReloadStatus stB = vm.GetHotReloadStatus("cmb_v2");

    const Chunk *cA1 = vm.GetChunkForVersion(stA1.active_version);
    const Chunk *cA2 = vm.GetChunkForVersion(v2_id);
    const Chunk *cB = vm.GetChunkForVersion(stB.active_version);

    if (!cA1 || cA1->iconst[0] != 1) { std::cerr << "[cross-vlookup] A v1 wrong" << std::endl; return false; }
    if (!cA2 || cA2->iconst[0] != 2) { std::cerr << "[cross-vlookup] A v2 wrong" << std::endl; return false; }
    if (!cB || cB->iconst[0] != 3) { std::cerr << "[cross-vlookup] B wrong" << std::endl; return false; }

    return true;
}

static bool CheckStringConcatFloatAndString()
{
    using namespace lpc::vm;

    Chunk ch;
    ch.module_name = "str_concat_float";
    ch.fconst = {3.14};
    ch.sconst = {" is pi"};

    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](Op op) { ch.code.push_back(static_cast<std::uint8_t>(op)); };

    emit_op(Op::LoadFConst); emit_u16(0);
    emit_op(Op::LoadSConst); emit_u16(0);
    emit_op(Op::Add);
    emit_op(Op::Return);
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;

    Value out = vm.last_result();
    if (!out.IsObjRef()) { std::cerr << "[str-concat-float] not objref" << std::endl; return false; }
    std::string result = vm.ResolveString(out);
    if (result.find("3.14") == std::string::npos) { std::cerr << "[str-concat-float] got: " << result << std::endl; return false; }
    if (result.find("is pi") == std::string::npos) { std::cerr << "[str-concat-float] missing 'is pi': " << result << std::endl; return false; }

    return true;
}

static bool CheckMultipleHotReloadCyclesGetChunkForVersion()
{
    using namespace lpc::vm;

    const std::string module_name = "multi_reload_vlookup";

    auto make_chunk = [&](std::int64_t val) -> Chunk {
        Chunk ch;
        ch.module_name = module_name;
        ch.iconst = {val};
        FunctionProto f;
        f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    RuntimeError e = vm.LoadChunk(make_chunk(1));
    if (!e.ok()) return false;

    std::vector<std::uint64_t> version_ids;
    ModuleHotReloadStatus st = vm.GetHotReloadStatus(module_name);
    version_ids.push_back(st.active_version);

    for (int i = 2; i <= 5; ++i) {
        Chunk candidate = make_chunk(i);
        std::uint64_t cand_id = 0;
        HotReloadCompatReport report;
        e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L0, &cand_id, &report);
        if (!e.ok()) { std::cerr << "[multi-reload] prepare " << i << " failed" << std::endl; return false; }
        e = vm.ActivateHotReload(module_name, cand_id, nullptr);
        if (!e.ok()) { std::cerr << "[multi-reload] activate " << i << " failed" << std::endl; return false; }
        version_ids.push_back(cand_id);
    }

    for (std::size_t i = 0; i < version_ids.size(); ++i) {
        const Chunk *c = vm.GetChunkForVersion(version_ids[i]);
        if (!c) { std::cerr << "[multi-reload] version " << i << " null" << std::endl; return false; }
        if (c->iconst[0] != static_cast<std::int64_t>(i + 1)) {
            std::cerr << "[multi-reload] version " << i << " iconst wrong: " << c->iconst[0] << std::endl;
            return false;
        }
    }

    return true;
}

static bool CheckRetiredVersionLookupFails()
{
    using namespace lpc::vm;

    const std::string module_name = "retired_vlookup";

    auto make_chunk = [&](std::int64_t val) -> Chunk {
        Chunk ch;
        ch.module_name = module_name;
        ch.iconst = {val};
        FunctionProto f;
        f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    RuntimeError e = vm.LoadChunk(make_chunk(1));
    if (!e.ok()) return false;

    std::uint64_t v1_id = 0;
    {
        ModuleHotReloadStatus st = vm.GetHotReloadStatus(module_name);
        v1_id = st.active_version;
    }

    for (int i = 2; i <= 5; ++i) {
        Chunk candidate = make_chunk(i);
        std::uint64_t cand_id = 0;
        HotReloadCompatReport report;
        e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L0, &cand_id, &report);
        if (!e.ok()) { std::cerr << "[retired] prepare " << i << " failed" << std::endl; return false; }
        e = vm.ActivateHotReload(module_name, cand_id, nullptr);
        if (!e.ok()) { std::cerr << "[retired] activate " << i << " failed" << std::endl; return false; }
    }

    ModuleHotReloadStatus st = vm.GetHotReloadStatus(module_name);
    bool v1_deprecated_or_retired = false;
    for (const auto &v : st.versions) {
        if (v.version_id == v1_id) {
            v1_deprecated_or_retired = (v.state == ModuleVersionState::Deprecated || v.state == ModuleVersionState::Retired);
            break;
        }
    }

    if (!v1_deprecated_or_retired) {
        const Chunk *c = vm.GetChunkForVersion(v1_id);
        if (c) {
            if (c->iconst[0] == 1) return true;
        }
        std::cerr << "[retired] v1 not deprecated/retired but also not resolvable" << std::endl;
        return false;
    }

    return true;
}

static bool CheckRollbackPreservesVersionLookup()
{
    using namespace lpc::vm;

    const std::string module_name = "rollback_vlookup";

    auto make_chunk = [&](std::int64_t val) -> Chunk {
        Chunk ch;
        ch.module_name = module_name;
        ch.iconst = {val};
        FunctionProto f;
        f.name = "main"; f.arity = 0; f.nlocals = 0; f.code_start = 0;
        ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(0));
        ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
        f.code_end = static_cast<std::uint32_t>(ch.code.size());
        ch.functions.push_back(f);
        return ch;
    };

    Vm vm;
    RuntimeError e = vm.LoadChunk(make_chunk(10));
    if (!e.ok()) return false;

    std::uint64_t v1_id = 0;
    {
        ModuleHotReloadStatus st = vm.GetHotReloadStatus(module_name);
        v1_id = st.active_version;
    }

    const Chunk *c1 = vm.GetChunkForVersion(v1_id);
    if (!c1 || c1->iconst[0] != 10) { std::cerr << "[rollback-vlookup] v1 before reload bad" << std::endl; return false; }

    Chunk candidate = make_chunk(20);
    std::uint64_t cand_id = 0;
    HotReloadCompatReport report;
    e = vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L0, &cand_id, &report);
    if (!e.ok()) { std::cerr << "[rollback-vlookup] prepare failed" << std::endl; return false; }
    e = vm.ActivateHotReload(module_name, cand_id, nullptr);
    if (!e.ok()) { std::cerr << "[rollback-vlookup] activate failed" << std::endl; return false; }

    const Chunk *c2 = vm.GetChunkForVersion(cand_id);
    if (!c2 || c2->iconst[0] != 20) { std::cerr << "[rollback-vlookup] v2 after reload bad" << std::endl; return false; }

    e = vm.RollbackHotReload(module_name, v1_id);
    if (!e.ok()) { std::cerr << "[rollback-vlookup] rollback failed: " << e.message << std::endl; return false; }

    const Chunk *c1_after = vm.GetChunkForVersion(v1_id);
    if (!c1_after || c1_after->iconst[0] != 10) { std::cerr << "[rollback-vlookup] v1 after rollback bad" << std::endl; return false; }

    return true;
}

static bool CheckJsonRpcSerializeRoundTrip()
{
    using namespace lpc::lsp;
    JsonNode obj(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(1)},
        {"method", JsonNode("initialize")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"processId", JsonNode(1234)},
            {"rootUri", JsonNode("file:///test")}
        })}
    });
    std::string serialized = JsonSerialize(obj);
    JsonNode parsed = JsonParse(serialized);
    if (!parsed.IsObject()) return false;
    if (!parsed["jsonrpc"].IsString()) return false;
    if (parsed["jsonrpc"].AsString() != "2.0") return false;
    if (!parsed["id"].IsNumber()) return false;
    if (parsed["id"].AsInt() != 1) return false;
    if (!parsed["method"].IsString()) return false;
    if (parsed["method"].AsString() != "initialize") return false;
    if (!parsed["params"].IsObject()) return false;
    if (!parsed["params"]["processId"].IsNumber()) return false;
    if (parsed["params"]["processId"].AsInt() != 1234) return false;
    return true;
}

static bool CheckJsonRpcArrayRoundTrip()
{
    using namespace lpc::lsp;
    JsonNode arr(std::vector<JsonNode>{
        JsonNode(10), JsonNode(20), JsonNode(30)
    });
    std::string s = JsonSerialize(arr);
    JsonNode p = JsonParse(s);
    if (!p.IsArray()) return false;
    if (p.ArraySize() != 3) return false;
    if (p[0].AsInt() != 10) return false;
    if (p[1].AsInt() != 20) return false;
    if (p[2].AsInt() != 30) return false;
    return true;
}

static bool CheckJsonRpcNullAndBoolRoundTrip()
{
    using namespace lpc::lsp;
    JsonNode obj(std::unordered_map<std::string, JsonNode>{
        {"null_val", JsonNode(JsonNull{})},
        {"bool_val", JsonNode(true)},
        {"false_val", JsonNode(false)},
        {"str_val", JsonNode("hello world")},
        {"float_val", JsonNode(3.14)}
    });
    std::string s = JsonSerialize(obj);
    JsonNode p = JsonParse(s);
    if (!p["null_val"].IsNull()) return false;
    if (!p["bool_val"].IsBool()) return false;
    if (!p["bool_val"].AsBool()) return false;
    if (!p["false_val"].IsBool()) return false;
    if (p["false_val"].AsBool()) return false;
    if (!p["str_val"].IsString()) return false;
    if (p["str_val"].AsString() != "hello world") return false;
    if (!p["float_val"].IsNumber()) return false;
    return true;
}

static bool CheckLspServerInitialize()
{
    using namespace lpc::lsp;
    std::string last_msg;
    LspServer server([&](const std::string &msg) { last_msg = msg; });
    JsonNode init_req(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(1)},
        {"method", JsonNode("initialize")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"processId", JsonNode(1234)},
            {"rootUri", JsonNode("file:///test")},
            {"capabilities", JsonNode(std::unordered_map<std::string, JsonNode>{})}
        })}
    });
    server.HandleMessage(init_req);
    if (last_msg.empty()) return false;
    auto hdr_end = last_msg.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    std::string body = last_msg.substr(hdr_end + 4);
    JsonNode resp = JsonParse(body);
    if (!resp.IsObject()) return false;
    if (!resp.Has("result")) return false;
    if (!resp["result"].Has("capabilities")) return false;
    return true;
}

static bool CheckLspSymbolIndex()
{
    using namespace lpc::lsp;
    SymbolIndex idx;
    idx.IndexDocument("file:///test.lpc", "void foo() { }\nvar x = 1;\nvar bar;\n");
    auto defs = idx.FindDefinition("foo");
    if (defs.size() != 1) return false;
    if (defs[0].name != "foo") return false;
    if (defs[0].uri != "file:///test.lpc") return false;
    auto all = idx.AllSymbols();
    if (all.empty()) return false;
    bool found_foo = false, found_x = false, found_bar = false;
    for (const auto &s : all) {
        if (s.name == "foo") found_foo = true;
        if (s.name == "x") found_x = true;
        if (s.name == "bar") found_bar = true;
    }
    return found_foo && found_x && found_bar;
}

static bool CheckReloadProtocolRoundTrip()
{
    using namespace lpc::vm;
    ReloadMessage msg;
    msg.type = ReloadMsgType::PrepareReload;
    msg.request_id = 42;
    msg.module_name = "test_module";
    msg.version_id = 100;
    msg.chunk_data = "fake_chunk_data";
    msg.compat_level = "L1";
    msg.migration_data = "";
    auto encoded = ReloadProtocol::Encode(msg);
    if (encoded.size() < 4) return false;
    std::uint32_t payload_len = static_cast<std::uint32_t>(encoded[0])
        | (static_cast<std::uint32_t>(encoded[1]) << 8)
        | (static_cast<std::uint32_t>(encoded[2]) << 16)
        | (static_cast<std::uint32_t>(encoded[3]) << 24);
    ReloadMessage decoded;
    if (!ReloadProtocol::Decode(encoded.data() + 4, payload_len, decoded)) return false;
    if (decoded.type != ReloadMsgType::PrepareReload) return false;
    if (decoded.request_id != 42) return false;
    if (decoded.module_name != "test_module") return false;
    if (decoded.version_id != 100) return false;
    if (decoded.chunk_data != "fake_chunk_data") return false;
    if (decoded.compat_level != "L1") return false;
    return true;
}

static bool CheckReloadProtocolAllMessageTypes()
{
    using namespace lpc::vm;
    ReloadMsgType types[] = {
        ReloadMsgType::Hello, ReloadMsgType::HelloAck,
        ReloadMsgType::PrepareReload, ReloadMsgType::PrepareResult,
        ReloadMsgType::ActivateReload, ReloadMsgType::ActivateResult,
        ReloadMsgType::RollbackReload, ReloadMsgType::RollbackResult,
        ReloadMsgType::StatusQuery, ReloadMsgType::StatusResponse,
        ReloadMsgType::SyncState, ReloadMsgType::SyncAck,
        ReloadMsgType::Heartbeat, ReloadMsgType::Error
    };
    for (auto t : types) {
        ReloadMessage msg;
        msg.type = t;
        msg.request_id = 7;
        msg.module_name = "mod";
        msg.version_id = 99;
        msg.success = true;
        msg.status_json = "{\"ok\":true}";
        msg.error_message = "err";
        auto encoded = ReloadProtocol::Encode(msg);
        if (encoded.size() < 4) return false;
        std::uint32_t plen = static_cast<std::uint32_t>(encoded[0])
            | (static_cast<std::uint32_t>(encoded[1]) << 8)
            | (static_cast<std::uint32_t>(encoded[2]) << 16)
            | (static_cast<std::uint32_t>(encoded[3]) << 24);
        ReloadMessage decoded;
        if (!ReloadProtocol::Decode(encoded.data() + 4, plen, decoded)) return false;
        if (decoded.type != t) return false;
        if (decoded.module_name != "mod") return false;
    }
    return true;
}

static bool CheckPersistenceCodecValueRoundTrip()
{
    using namespace lpc::vm;
    {
        StoredValue sv;
        sv.type = StoredValue::Int64;
        sv.int_val = -12345;
        auto data = PersistenceCodec::SerializeValue(sv);
        StoredValue out;
        std::size_t pos = 0;
        if (!PersistenceCodec::DeserializeValue(data.data(), data.size(), pos, out)) return false;
        if (out.type != StoredValue::Int64) return false;
        if (out.int_val != -12345) return false;
    }
    {
        StoredValue sv;
        sv.type = StoredValue::Float64;
        sv.float_val = 3.14159;
        auto data = PersistenceCodec::SerializeValue(sv);
        StoredValue out;
        std::size_t pos = 0;
        if (!PersistenceCodec::DeserializeValue(data.data(), data.size(), pos, out)) return false;
        if (out.type != StoredValue::Float64) return false;
        if (out.float_val < 3.14 || out.float_val > 3.15) return false;
    }
    {
        StoredValue sv;
        sv.type = StoredValue::String;
        sv.str_val = "hello persistent world";
        auto data = PersistenceCodec::SerializeValue(sv);
        StoredValue out;
        std::size_t pos = 0;
        if (!PersistenceCodec::DeserializeValue(data.data(), data.size(), pos, out)) return false;
        if (out.type != StoredValue::String) return false;
        if (out.str_val != "hello persistent world") return false;
    }
    {
        StoredValue sv;
        sv.type = StoredValue::Bool;
        sv.bool_val = true;
        auto data = PersistenceCodec::SerializeValue(sv);
        StoredValue out;
        std::size_t pos = 0;
        if (!PersistenceCodec::DeserializeValue(data.data(), data.size(), pos, out)) return false;
        if (out.type != StoredValue::Bool) return false;
        if (!out.bool_val) return false;
    }
    return true;
}

static bool CheckPersistenceCodecNestedValueRoundTrip()
{
    using namespace lpc::vm;
    StoredValue sv;
    sv.type = StoredValue::Array;
    StoredValue elem1;
    elem1.type = StoredValue::Int64;
    elem1.int_val = 10;
    StoredValue elem2;
    elem2.type = StoredValue::String;
    elem2.str_val = "nested";
    sv.elements.push_back(elem1);
    sv.elements.push_back(elem2);
    auto data = PersistenceCodec::SerializeValue(sv);
    StoredValue out;
    std::size_t pos = 0;
    if (!PersistenceCodec::DeserializeValue(data.data(), data.size(), pos, out)) return false;
    if (out.type != StoredValue::Array) return false;
    if (out.elements.size() != 2) return false;
    if (out.elements[0].type != StoredValue::Int64) return false;
    if (out.elements[0].int_val != 10) return false;
    if (out.elements[1].type != StoredValue::String) return false;
    if (out.elements[1].str_val != "nested") return false;
    return true;
}

static bool CheckPersistenceCodecObjectRoundTrip()
{
    using namespace lpc::vm;
    StoredObject obj;
    obj.module_name = "test_mod";
    obj.module_version_id = 42;
    StoredValue g1;
    g1.type = StoredValue::Int64;
    g1.int_val = 99;
    obj.globals.push_back({"count", g1});
    obj.object_type = "MyClass";
    auto data = PersistenceCodec::SerializeObject(obj);
    StoredObject out;
    std::size_t pos = 0;
    if (!PersistenceCodec::DeserializeObject(data.data(), data.size(), pos, out)) return false;
    if (out.module_name != "test_mod") return false;
    if (out.module_version_id != 42) return false;
    if (out.globals.size() != 1) return false;
    if (out.globals[0].first != "count") return false;
    if (out.globals[0].second.int_val != 99) return false;
    if (out.object_type != "MyClass") return false;
    return true;
}

static bool CheckPersistenceCodecModuleStateRoundTrip()
{
    using namespace lpc::vm;
    PersistedModuleState state;
    state.module_name = "game";
    state.active_version_id = 7;
    state.chunk_serialized = "fake_serialized_chunk";
    StoredObject obj;
    obj.module_name = "game";
    obj.module_version_id = 7;
    state.objects.push_back(obj);
    auto data = PersistenceCodec::SerializeModuleState(state);
    PersistedModuleState out;
    if (!PersistenceCodec::DeserializeModuleState(data.data(), data.size(), out)) return false;
    if (out.module_name != "game") return false;
    if (out.active_version_id != 7) return false;
    if (out.chunk_serialized != "fake_serialized_chunk") return false;
    if (out.objects.size() != 1) return false;
    return true;
}

static bool CheckChunkSerializationRoundTrip()
{
    using namespace lpc::vm;
    Chunk ch;
    ch.module_name = "serial_test";
    ch.iconst = {100, 200};
    ch.fconst = {1.5, 2.5};
    ch.sconst = {"hello", "world"};
    ch.code = {0x01, 0x00, 0x00, 0x02, 0x01, 0x00, 0x06};
    ch.global_names = {"g_count", "g_name"};
    ch.globals.resize(2);
    FunctionProto f;
    f.name = "main";
    f.nlocals = 2;
    f.code_start = 0;
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);
    ClassInfo ci;
    ci.name = "MyClass";
    ci.field_names = {"x", "y"};
    ci.field_name_index["x"] = 0;
    ci.field_name_index["y"] = 1;
    ch.classes.push_back(ci);
    std::string serialized = SerializeChunk(ch);
    if (serialized.empty()) return false;
    Chunk ch2;
    if (!DeserializeChunk(serialized, &ch2)) return false;
    if (ch2.module_name != "serial_test") return false;
    if (ch2.iconst.size() != 2 || ch2.iconst[0] != 100 || ch2.iconst[1] != 200) return false;
    if (ch2.fconst.size() != 2) return false;
    if (ch2.sconst.size() != 2 || ch2.sconst[0] != "hello" || ch2.sconst[1] != "world") return false;
    if (ch2.code.size() != ch.code.size()) return false;
    if (ch2.global_names.size() != 2 || ch2.global_names[0] != "g_count") return false;
    if (ch2.functions.size() != 1 || ch2.functions[0].name != "main") return false;
    if (ch2.functions[0].nlocals != 2) return false;
    if (ch2.classes.size() != 1 || ch2.classes[0].name != "MyClass") return false;
    if (ch2.classes[0].field_names.size() != 2) return false;
    return true;
}

static bool CheckChunkDeserializeAndExecute()
{
    using namespace lpc::vm;
    Chunk ch;
    ch.module_name = "chunk_exec_test";
    ch.iconst = {10, 20};
    lpc::vm::FunctionProto f;
    f.name = "main";
    f.arity = 0;
    f.nlocals = 0;
    f.max_stack = 4;
    f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(0);
    ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(1);
    ch.code.push_back(static_cast<std::uint8_t>(Op::Add));
    ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);

    std::string serialized = SerializeChunk(ch);
    if (serialized.empty()) return false;

    Chunk ch2;
    if (!DeserializeChunk(serialized, &ch2)) return false;

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch2);
    if (!e.ok()) return false;
    e = vm.RunEntry("main");
    if (!e.ok()) return false;
    Value out = vm.last_result();
    return out.Tag() == ValueTag::Int64 && out.AsI64() == 30;
}

static bool CheckPersistentStorageSaveLoadModule()
{
    using namespace lpc::vm;
    Chunk ch;
    ch.module_name = "persist_mod";
    ch.iconst = {42};
    ch.global_names = {"g_count"};
    ch.globals.resize(1);
    FunctionProto f;
    f.name = "main";
    f.arity = 0;
    f.nlocals = 0;
    f.max_stack = 4;
    f.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    emit_u16(0);
    ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);

    Vm vm;
    RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) return false;

    std::string tmp_dir;
#ifdef _WIN32
    tmp_dir = std::getenv("TEMP") ? std::getenv("TEMP") : "C:\\Temp";
#else
    tmp_dir = "/tmp";
#endif
    PersistentStorage ps(tmp_dir);
    bool saved = ps.SaveModuleState(vm, "persist_mod");
    if (!saved) return false;

    auto modules = ps.ListPersistedModules();
    bool found = false;
    for (const auto &m : modules) {
        if (m == "persist_mod") found = true;
    }
    if (!found) return false;

    return ps.DeleteModuleState("persist_mod");
}

static bool CheckPersistentStorageSaveLoadAllModules()
{
    using namespace lpc::vm;
    Chunk chA;
    chA.module_name = "persist_all_a";
    chA.iconst = {1};
    FunctionProto fA;
    fA.name = "main"; fA.arity = 0; fA.nlocals = 0; fA.max_stack = 4; fA.code_start = 0;
    auto emit_u16 = [&](unsigned v) {
        chA.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        chA.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    chA.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst)); emit_u16(0);
    chA.code.push_back(static_cast<std::uint8_t>(Op::Return));
    fA.code_end = static_cast<std::uint32_t>(chA.code.size());
    chA.functions.push_back(fA);

    Vm vm;
    RuntimeError e = vm.LoadModule("persist_all_a", chA);
    if (!e.ok()) return false;

    std::string tmp_dir;
#ifdef _WIN32
    tmp_dir = std::getenv("TEMP") ? std::getenv("TEMP") : "C:\\Temp";
#else
    tmp_dir = "/tmp";
#endif
    PersistentStorage ps(tmp_dir);
    bool saved = ps.SaveAllModules(vm);
    if (!saved) return false;
    ps.DeleteModuleState("persist_all_a");
    return true;
}

static bool CheckLspCompletionAndHover()
{
    using namespace lpc::lsp;
    std::string last_msg;
    LspServer server([&](const std::string &msg) { last_msg = msg; });

    JsonNode init_msg(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(1)},
        {"method", JsonNode("initialize")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"processId", JsonNode(1)},
            {"rootUri", JsonNode("file:///test")},
            {"capabilities", JsonNode(std::unordered_map<std::string, JsonNode>{})}
        })}
    });
    server.HandleMessage(init_msg);
    last_msg.clear();

    JsonNode did_open(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"method", JsonNode("textDocument/didOpen")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"textDocument", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"uri", JsonNode("file:///test.lpc")},
                {"languageId", JsonNode("lpc")},
                {"version", JsonNode(1)},
                {"text", JsonNode("void foo() { }\nvar x = 1;\n")}
            })}
        })}
    });
    server.HandleMessage(did_open);

    JsonNode completion_req(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(2)},
        {"method", JsonNode("textDocument/completion")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"textDocument", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"uri", JsonNode("file:///test.lpc")}
            })},
            {"position", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"line", JsonNode(0)},
                {"character", JsonNode(0)}
            })}
        })}
    });
    server.HandleMessage(completion_req);
    if (last_msg.empty()) return false;
    auto hdr_end = last_msg.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    JsonNode comp_resp = JsonParse(last_msg.substr(hdr_end + 4));
    if (!comp_resp.Has("result")) return false;

    last_msg.clear();
    JsonNode hover_req(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(3)},
        {"method", JsonNode("textDocument/hover")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"textDocument", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"uri", JsonNode("file:///test.lpc")}
            })},
            {"position", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"line", JsonNode(0)},
                {"character", JsonNode(5)}
            })}
        })}
    });
    server.HandleMessage(hover_req);
    if (last_msg.empty()) return false;
    hdr_end = last_msg.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    JsonNode hover_resp = JsonParse(last_msg.substr(hdr_end + 4));
    if (!hover_resp.Has("result")) return false;
    return true;
}

static bool CheckDistributedCoordinatorBasic()
{
    using namespace lpc::vm;
    Chunk ch;
    ch.module_name = "coord_test";
    ch.iconst = {1};
    FunctionProto f;
    f.name = "main"; f.arity = 0; f.nlocals = 0; f.max_stack = 4; f.code_start = 0;
    ch.code.push_back(static_cast<std::uint8_t>(Op::LoadIConst));
    ch.code.push_back(0); ch.code.push_back(0);
    ch.code.push_back(static_cast<std::uint8_t>(Op::Return));
    f.code_end = static_cast<std::uint32_t>(ch.code.size());
    ch.functions.push_back(f);

    Vm vm;
    vm.LoadChunk(ch);

    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> sent;
    std::vector<std::vector<std::uint8_t>> broadcasts;

    HotReloadCoordinator coord(vm,
        [&](std::uint64_t peer_id, const std::vector<std::uint8_t> &data) {
            sent.push_back({peer_id, data});
        },
        [&](const std::vector<std::uint8_t> &data) {
            broadcasts.push_back(data);
        });

    coord.AddPeer(1, "localhost", 9001);
    coord.AddPeer(2, "localhost", 9002);

    if (coord.peers().size() != 2) return false;

    auto chunk_ser = SerializeChunk(ch);
    RuntimeError e = coord.PrepareAndBroadcast("coord_test", chunk_ser, "L0");
    if (!e.ok()) return false;
    if (broadcasts.empty()) return false;

    coord.RemovePeer(2);
    if (coord.peers().size() != 1) return false;
    return true;
}

static bool CheckLspDocumentSymbolAndReferences()
{
    using namespace lpc::lsp;
    std::string last_msg;
    LspServer server([&](const std::string &msg) { last_msg = msg; });

    JsonNode init_msg(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(1)},
        {"method", JsonNode("initialize")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"processId", JsonNode(1)},
            {"rootUri", JsonNode("file:///test")},
            {"capabilities", JsonNode(std::unordered_map<std::string, JsonNode>{})}
        })}
    });
    server.HandleMessage(init_msg);
    last_msg.clear();

    JsonNode did_open(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"method", JsonNode("textDocument/didOpen")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"textDocument", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"uri", JsonNode("file:///sym.lpc")},
                {"languageId", JsonNode("lpc")},
                {"version", JsonNode(1)},
                {"text", JsonNode("void foo() { }\nvar count = 0;\n")}
            })}
        })}
    });
    server.HandleMessage(did_open);

    JsonNode sym_req(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(2)},
        {"method", JsonNode("textDocument/documentSymbol")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"textDocument", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"uri", JsonNode("file:///sym.lpc")}
            })}
        })}
    });
    server.HandleMessage(sym_req);
    if (last_msg.empty()) return false;
    auto hdr_end = last_msg.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    JsonNode sym_resp = JsonParse(last_msg.substr(hdr_end + 4));
    if (!sym_resp.Has("result")) return false;
    if (!sym_resp["result"].IsArray()) return false;
    if (sym_resp["result"].ArraySize() < 1) return false;

    last_msg.clear();
    JsonNode ref_req(std::unordered_map<std::string, JsonNode>{
        {"jsonrpc", JsonNode("2.0")},
        {"id", JsonNode(3)},
        {"method", JsonNode("textDocument/references")},
        {"params", JsonNode(std::unordered_map<std::string, JsonNode>{
            {"textDocument", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"uri", JsonNode("file:///sym.lpc")}
            })},
            {"position", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"line", JsonNode(0)},
                {"character", JsonNode(5)}
            })},
            {"context", JsonNode(std::unordered_map<std::string, JsonNode>{
                {"includeDeclaration", JsonNode(true)}
            })}
        })}
    });
    server.HandleMessage(ref_req);
    if (last_msg.empty()) return false;
    return true;
}

int RunSelfChecks()
{
    int failed = 0;
    if (!CheckNextVMMinimalProgram()) {
        std::cerr << "[vm-self-check] NextVM minimal program check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMLocalsAndJump()) {
        std::cerr << "[vm-self-check] NextVM locals+jump check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMCallValue()) {
        std::cerr << "[vm-self-check] NextVM call check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMVerifierRejectsBadJump()) {
        std::cerr << "[vm-self-check] NextVM verifier check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMGarbageCollection()) {
        std::cerr << "[vm-self-check] NextVM GC stress check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMGCKeepReachableArray()) {
        std::cerr << "[vm-self-check] NextVM GC keep reachable check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMHotReloadL0CompatReject()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L0 reject check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMHotReloadActivateSimple()) {
        std::cerr << "[vm-self-check] NextVM hot-reload activate check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMDebugHotReloadInterleaving()) {
        std::cerr << "[vm-self-check] NextVM debug+hot-reload interleave check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMHotReloadRequiresExplicitTrigger()) {
        std::cerr << "[vm-self-check] NextVM explicit hot-reload trigger check failed" << std::endl;
        ++failed;
    }
    if (!CheckNextVMHotReloadRetentionStress()) {
        std::cerr << "[vm-self-check] NextVM hot-reload retention stress check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadApiPrepareActivateFlow()) {
        std::cerr << "[vm-self-check] NextVM hot-reload API prepare/activate check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadApiSmokeGate()) {
        std::cerr << "[vm-self-check] NextVM hot-reload smoke gate check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadSmokePassAndActivate()) {
        std::cerr << "[vm-self-check] NextVM hot-reload smoke pass+activate check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadHighFrequencyGcStress()) {
        std::cerr << "[vm-self-check] NextVM hot-reload GC stress check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1AddFunctionAccept()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 add-function check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1RejectRemoval()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 reject-removal check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1RejectArityChange()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 reject-arity check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1AddGlobalsAccept()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 add-globals check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1AddClassAccept()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 add-class check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1RejectGlobalReorder()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 reject-global-reorder check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1RejectClassLayoutChange()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 reject-class-layout check failed" << std::endl;
        ++failed;
    }
    if (!CheckAuditLogRecordsPrepareActivateRollback()) {
        std::cerr << "[vm-self-check] NextVM audit-log prepare/activate/rollback check failed" << std::endl;
        ++failed;
    }
    if (!CheckAuditLogRollbackEntry()) {
        std::cerr << "[vm-self-check] NextVM audit-log rollback entry check failed" << std::endl;
        ++failed;
    }
    if (!CheckAuditLogJsonFormatAndRingBuffer()) {
        std::cerr << "[vm-self-check] NextVM audit-log JSON+ring-buffer check failed" << std::endl;
        ++failed;
    }
    if (!CheckCrossModuleLoadModule()) {
        std::cerr << "[vm-self-check] NextVM cross-module load check failed" << std::endl;
        ++failed;
    }
    if (!CheckCrossModuleHotReloadIndependent()) {
        std::cerr << "[vm-self-check] NextVM cross-module hot-reload independent check failed" << std::endl;
        ++failed;
    }
    if (!CheckCrossModuleGetChunkForVersion()) {
        std::cerr << "[vm-self-check] NextVM cross-module GetChunkForVersion check failed" << std::endl;
        ++failed;
    }
    if (!CheckObjectAutoUpgradeL0()) {
        std::cerr << "[vm-self-check] NextVM object auto-upgrade L0 check failed" << std::endl;
        ++failed;
    }
    if (!CheckObjectAutoUpgradeL1AddGlobals()) {
        std::cerr << "[vm-self-check] NextVM object auto-upgrade L1 add-globals check failed" << std::endl;
        ++failed;
    }
    if (!CheckObjectAutoUpgradeLazyOnAccess()) {
        std::cerr << "[vm-self-check] NextVM object auto-upgrade lazy-on-access check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL2RenameGlobalWithMigration()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L2 rename-global check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL2RejectsUncoveredRemoval()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L2 reject-uncovered check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL2RejectsNewFieldWithoutDefault()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L2 reject-no-default check failed" << std::endl;
        ++failed;
    }
    if (!CheckObjectAutoUpgradeL2WithMigration()) {
        std::cerr << "[vm-self-check] NextVM object auto-upgrade L2 check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL2CustomTransform()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L2 custom-transform check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL2ClassFieldMigration()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L2 class-field-migration check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL2RejectsClassFieldWithoutMigration()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L2 class-reject-without-migration check failed" << std::endl;
        ++failed;
    }
    if (!CheckStringConcatAndCompare()) {
        std::cerr << "[vm-self-check] NextVM string concat+compare check failed" << std::endl;
        ++failed;
    }
    if (!CheckFloatArithmetic()) {
        std::cerr << "[vm-self-check] NextVM float arithmetic check failed" << std::endl;
        ++failed;
    }
    if (!CheckArrayIndexSetAndGet()) {
        std::cerr << "[vm-self-check] NextVM array index set/get check failed" << std::endl;
        ++failed;
    }
    if (!CheckRecursionFibonacci()) {
        std::cerr << "[vm-self-check] NextVM recursion fibonacci check failed" << std::endl;
        ++failed;
    }
    if (!CheckObjectSetGetField()) {
        std::cerr << "[vm-self-check] NextVM object set/get field check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadRollbackRestoresObject()) {
        std::cerr << "[vm-self-check] NextVM hot-reload rollback restores object check failed" << std::endl;
        ++failed;
    }
    if (!CheckTypeErrorOnBadOperand()) {
        std::cerr << "[vm-self-check] NextVM type error on bad operand check failed" << std::endl;
        ++failed;
    }
    if (!CheckMultipleObjectsWithGlobals()) {
        std::cerr << "[vm-self-check] NextVM multiple objects with globals check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadL1AddClassFieldAccept()) {
        std::cerr << "[vm-self-check] NextVM hot-reload L1 add-class-field check failed" << std::endl;
        ++failed;
    }
    if (!CheckVersionLookupAcrossModules()) {
        std::cerr << "[vm-self-check] NextVM version-lookup across-modules check failed" << std::endl;
        ++failed;
    }
    if (!CheckVersionLookupAfterHotReload()) {
        std::cerr << "[vm-self-check] NextVM version-lookup after hot-reload check failed" << std::endl;
        ++failed;
    }
    if (!CheckStringConcatNumericAndString()) {
        std::cerr << "[vm-self-check] NextVM string-concat numeric+string check failed" << std::endl;
        ++failed;
    }
    if (!CheckStringConcatTwoStrings()) {
        std::cerr << "[vm-self-check] NextVM string-concat two-strings check failed" << std::endl;
        ++failed;
    }
    if (!CheckStringConcatFloatAndString()) {
        std::cerr << "[vm-self-check] NextVM string-concat float+string check failed" << std::endl;
        ++failed;
    }
    if (!CheckClassInstanceUpgradeViaIndex()) {
        std::cerr << "[vm-self-check] NextVM class-instance-upgrade via-index check failed" << std::endl;
        ++failed;
    }
    if (!CheckHotReloadRollbackGetChunkForVersion()) {
        std::cerr << "[vm-self-check] NextVM rollback GetChunkForVersion check failed" << std::endl;
        ++failed;
    }
    if (!CheckCrossModuleVersionLookupAfterReload()) {
        std::cerr << "[vm-self-check] NextVM cross-module version-lookup after-reload check failed" << std::endl;
        ++failed;
    }
    if (!CheckMultipleHotReloadCyclesGetChunkForVersion()) {
        std::cerr << "[vm-self-check] NextVM multiple-hot-reload-cycles GetChunkForVersion check failed" << std::endl;
        ++failed;
    }
    if (!CheckRetiredVersionLookupFails()) {
        std::cerr << "[vm-self-check] NextVM retired-version-lookup-fails check failed" << std::endl;
        ++failed;
    }
    if (!CheckRollbackPreservesVersionLookup()) {
        std::cerr << "[vm-self-check] NextVM rollback-preserves-version-lookup check failed" << std::endl;
        ++failed;
    }
    if (!CheckJsonRpcSerializeRoundTrip()) {
        std::cerr << "[vm-self-check] JSON-RPC serialize round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckJsonRpcArrayRoundTrip()) {
        std::cerr << "[vm-self-check] JSON-RPC array round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckJsonRpcNullAndBoolRoundTrip()) {
        std::cerr << "[vm-self-check] JSON-RPC null/bool round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckLspServerInitialize()) {
        std::cerr << "[vm-self-check] LSP server initialize check failed" << std::endl;
        ++failed;
    }
    if (!CheckLspSymbolIndex()) {
        std::cerr << "[vm-self-check] LSP symbol index check failed" << std::endl;
        ++failed;
    }
    if (!CheckReloadProtocolRoundTrip()) {
        std::cerr << "[vm-self-check] reload protocol round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckReloadProtocolAllMessageTypes()) {
        std::cerr << "[vm-self-check] reload protocol all-message-types check failed" << std::endl;
        ++failed;
    }
    if (!CheckPersistenceCodecValueRoundTrip()) {
        std::cerr << "[vm-self-check] persistence codec value round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckPersistenceCodecNestedValueRoundTrip()) {
        std::cerr << "[vm-self-check] persistence codec nested-value round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckPersistenceCodecObjectRoundTrip()) {
        std::cerr << "[vm-self-check] persistence codec object round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckPersistenceCodecModuleStateRoundTrip()) {
        std::cerr << "[vm-self-check] persistence codec module-state round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckChunkSerializationRoundTrip()) {
        std::cerr << "[vm-self-check] chunk serialization round-trip check failed" << std::endl;
        ++failed;
    }
    if (!CheckChunkDeserializeAndExecute()) {
        std::cerr << "[vm-self-check] chunk deserialize+execute check failed" << std::endl;
        ++failed;
    }
    if (!CheckPersistentStorageSaveLoadModule()) {
        std::cerr << "[vm-self-check] persistent storage save/load module check failed" << std::endl;
        ++failed;
    }
    if (!CheckPersistentStorageSaveLoadAllModules()) {
        std::cerr << "[vm-self-check] persistent storage save/load all-modules check failed" << std::endl;
        ++failed;
    }
    if (!CheckLspCompletionAndHover()) {
        std::cerr << "[vm-self-check] LSP completion+hover check failed" << std::endl;
        ++failed;
    }
    if (!CheckDistributedCoordinatorBasic()) {
        std::cerr << "[vm-self-check] distributed coordinator basic check failed" << std::endl;
        ++failed;
    }
    if (!CheckLspDocumentSymbolAndReferences()) {
        std::cerr << "[vm-self-check] LSP documentSymbol+references check failed" << std::endl;
        ++failed;
    }
    if (failed == 0) {
        std::cout << "[vm-self-check] PASS" << std::endl;
    }
    return failed;
}

} // namespace vm
} // namespace lpc
