#include <iostream>
#include <vector>
#include <string>

#include "runtime/vm.h"
#include "vm2/runtime/vm.h"
#include "vm2/bytecode/opcode.h"

namespace lpc {
namespace vmtest {

static bool CheckFrameInfoApi()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    vm_frame_info_t info;
    bool ok = !vm->get_frame_info(nullptr, &info);
    return ok;
}

static bool CheckVm2MinimalProgram()
{
    lpc::vm2::Chunk ch;
    ch.module_name = "self_check";
    ch.iconst = {1, 2};
    lpc::vm2::FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    ch.code.push_back(static_cast<std::uint8_t>(lpc::vm2::Op::LoadIConst));
    emit_u16(0);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::vm2::Op::LoadIConst));
    emit_u16(1);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::vm2::Op::Add));
    ch.code.push_back(static_cast<std::uint8_t>(lpc::vm2::Op::Return));
    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::vm2::Vm vm;
    lpc::vm2::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        return false;
    }

    lpc::vm2::Value out = vm.last_result();
    return out.tag == lpc::vm2::ValueTag::Int64 && out.as.i64 == 3;
}

static bool CheckVm2LocalsAndJump()
{
    lpc::vm2::Chunk ch;
    ch.module_name = "self_check_locals";
    ch.iconst = {5, 2, 0};
    lpc::vm2::FunctionProto f;
    f.name = "main";
    f.nlocals = 1;
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](lpc::vm2::Op op) {
        ch.code.push_back(static_cast<std::uint8_t>(op));
    };

    emit_op(lpc::vm2::Op::LoadIConst);
    emit_u16(0);
    emit_op(lpc::vm2::Op::StoreLocal);
    emit_u16(0);

    emit_op(lpc::vm2::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::vm2::Op::JumpIfFalse);
    emit_u16(6);

    emit_op(lpc::vm2::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::vm2::Op::Jump);
    emit_u16(3);

    emit_op(lpc::vm2::Op::LoadIConst);
    emit_u16(2);

    emit_op(lpc::vm2::Op::LoadLocal);
    emit_u16(0);
    emit_op(lpc::vm2::Op::Add);
    emit_op(lpc::vm2::Op::Return);

    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::vm2::Vm vm;
    lpc::vm2::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[vm2 locals+jump] error: " << e.message << std::endl;
        return false;
    }
    lpc::vm2::Value out = vm.last_result();
    if (!(out.tag == lpc::vm2::ValueTag::Int64 && out.as.i64 == 7)) {
        std::cerr << "[vm2 locals+jump] result tag=" << static_cast<int>(out.tag)
                  << " value=" << out.as.i64 << std::endl;
    }
    return out.tag == lpc::vm2::ValueTag::Int64 && out.as.i64 == 7;
}

static bool CheckVm2CallValue()
{
    lpc::vm2::Chunk ch;
    ch.module_name = "self_check_call";
    ch.iconst = {2, 4};

    lpc::vm2::FunctionProto add;
    add.name = "add2";
    add.arity = 2;
    add.nlocals = 2;
    add.code_start = 0;

    lpc::vm2::FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](lpc::vm2::Op op) {
        ch.code.push_back(static_cast<std::uint8_t>(op));
    };

    emit_op(lpc::vm2::Op::LoadLocal);
    emit_u16(0);
    emit_op(lpc::vm2::Op::LoadLocal);
    emit_u16(1);
    emit_op(lpc::vm2::Op::Add);
    emit_op(lpc::vm2::Op::Return);
    add.code_end = static_cast<std::uint32_t>(ch.code.size());

    mainf.code_start = add.code_end;
    emit_op(lpc::vm2::Op::LoadIConst);
    emit_u16(0);
    emit_op(lpc::vm2::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::vm2::Op::CallValue);
    emit_u16(0);
    emit_u16(2);
    emit_op(lpc::vm2::Op::Return);
    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());

    ch.functions.push_back(add);
    ch.functions.push_back(mainf);

    lpc::vm2::Vm vm;
    lpc::vm2::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[vm2 call] error: " << e.message << std::endl;
        return false;
    }
    lpc::vm2::Value out = vm.last_result();
    if (!(out.tag == lpc::vm2::ValueTag::Int64 && out.as.i64 == 6)) {
        std::cerr << "[vm2 call] bad result tag=" << static_cast<int>(out.tag)
                  << " value=" << out.as.i64 << std::endl;
    }
    return out.tag == lpc::vm2::ValueTag::Int64 && out.as.i64 == 6;
}

static bool CheckVm2VerifierRejectsBadJump()
{
    lpc::vm2::Chunk ch;
    ch.module_name = "self_check_verify";
    lpc::vm2::FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    ch.code.push_back(static_cast<std::uint8_t>(lpc::vm2::Op::Jump));
    emit_u16(200);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::vm2::Op::Return));
    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::vm2::Vm vm;
    lpc::vm2::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    return !e.ok();
}

static bool CheckStackOverflowNonFatalReportsTrace()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    vm->set_non_fatal_mode(true);
    vm->clear_last_error();
    bool caught = false;
    try {
        vm->stack_overflow();
    } catch (const vm_abort_signal &) {
        caught = true;
    }

    if (!caught || !vm->has_error()) {
        return false;
    }
    const std::string &err = vm->last_error();
    return err.find("stack overflow") != std::string::npos;
}

static bool CheckGcStatsApi()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }
    if (vm->gc_collect_count() != 0) {
        return false;
    }
    if (vm->gc_total_freed_bytes() != 0) {
        return false;
    }
    if (vm->gc_last_freed_bytes() != 0) {
        return false;
    }
    if (vm->gc_last_collected_objects() != 0) {
        return false;
    }
    if (vm->gc_total_collected_objects() != 0) {
        return false;
    }
    if (vm->gc_minor_collect_count() != 0) {
        return false;
    }
    if (vm->gc_nursery_bytes() > vm->gc_nursery_limit_bytes()) {
        return false;
    }
    if (vm->gc_remembered_set_size() != 0) {
        return false;
    }
    if (vm->gc_nursery_limit_bytes() == 0) {
        return false;
    }
    return true;
}

static bool CheckNurseryLimitConfig()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }
    const luint64_t oldv = vm->gc_nursery_limit_bytes();
    vm->set_nursery_limit_bytes(1024);
    const luint64_t newv = vm->gc_nursery_limit_bytes();
    if (newv != 1024) {
        return false;
    }
    vm->set_nursery_limit_bytes(oldv);
    return true;
}

int RunSelfChecks()
{
    int failed = 0;
    if (!CheckFrameInfoApi()) {
        std::cerr << "[vm-self-check] frame info api check failed" << std::endl;
        ++failed;
    }
    if (!CheckVm2MinimalProgram()) {
        std::cerr << "[vm-self-check] vm2 minimal program check failed" << std::endl;
        ++failed;
    }
    if (!CheckVm2LocalsAndJump()) {
        std::cerr << "[vm-self-check] vm2 locals+jump check failed" << std::endl;
        ++failed;
    }
    if (!CheckVm2CallValue()) {
        std::cerr << "[vm-self-check] vm2 call check failed" << std::endl;
        ++failed;
    }
    if (!CheckVm2VerifierRejectsBadJump()) {
        std::cerr << "[vm-self-check] vm2 verifier check failed" << std::endl;
        ++failed;
    }
    if (!CheckStackOverflowNonFatalReportsTrace()) {
        std::cerr << "[vm-self-check] stack overflow non-fatal check failed" << std::endl;
        ++failed;
    }
    if (!CheckGcStatsApi()) {
        std::cerr << "[vm-self-check] gc stats api check failed" << std::endl;
        ++failed;
    }
    if (!CheckNurseryLimitConfig()) {
        std::cerr << "[vm-self-check] nursery limit config check failed" << std::endl;
        ++failed;
    }
    if (failed == 0) {
        std::cout << "[vm-self-check] PASS" << std::endl;
    }
    return failed;
}

} // namespace vmtest
} // namespace lpc
