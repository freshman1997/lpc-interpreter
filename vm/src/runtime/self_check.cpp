#include <iostream>
#include <vector>
#include <string>

#include "runtime/vm.h"
#include "runtime/verifier.h"
#include "gc/gc.h"
#include "opcode.h"
#include "lpc_value.h"
#include "memory/memory.h"
#include "type/lpc_array.h"
#include "type/lpc_closure.h"
#include "type/lpc_mapping.h"
#include "type/lpc_proto.h"
#include "nextvm/runtime/vm.h"
#include "nextvm/bytecode/opcode.h"

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

static bool CheckNextVMMinimalProgram()
{
    lpc::nextvm::Chunk ch;
    ch.module_name = "self_check";
    ch.iconst = {1, 2};
    lpc::nextvm::FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    ch.code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LoadIConst));
    emit_u16(0);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::LoadIConst));
    emit_u16(1);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Add));
    ch.code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Return));
    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::nextvm::Vm vm;
    lpc::nextvm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        return false;
    }

    lpc::nextvm::Value out = vm.last_result();
    return out.tag == lpc::nextvm::ValueTag::Int64 && out.as.i64 == 3;
}

static bool CheckNextVMLocalsAndJump()
{
    lpc::nextvm::Chunk ch;
    ch.module_name = "self_check_locals";
    ch.iconst = {5, 2, 0};
    lpc::nextvm::FunctionProto f;
    f.name = "main";
    f.nlocals = 1;
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](lpc::nextvm::Op op) {
        ch.code.push_back(static_cast<std::uint8_t>(op));
    };

    emit_op(lpc::nextvm::Op::LoadIConst);
    emit_u16(0);
    emit_op(lpc::nextvm::Op::StoreLocal);
    emit_u16(0);

    emit_op(lpc::nextvm::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::nextvm::Op::JumpIfFalse);
    emit_u16(6);

    emit_op(lpc::nextvm::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::nextvm::Op::Jump);
    emit_u16(3);

    emit_op(lpc::nextvm::Op::LoadIConst);
    emit_u16(2);

    emit_op(lpc::nextvm::Op::LoadLocal);
    emit_u16(0);
    emit_op(lpc::nextvm::Op::Add);
    emit_op(lpc::nextvm::Op::Return);

    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::nextvm::Vm vm;
    lpc::nextvm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM locals+jump] error: " << e.message << std::endl;
        return false;
    }
    lpc::nextvm::Value out = vm.last_result();
    if (!(out.tag == lpc::nextvm::ValueTag::Int64 && out.as.i64 == 7)) {
        std::cerr << "[NextVM locals+jump] result tag=" << static_cast<int>(out.tag)
                  << " value=" << out.as.i64 << std::endl;
    }
    return out.tag == lpc::nextvm::ValueTag::Int64 && out.as.i64 == 7;
}

static bool CheckNextVMCallValue()
{
    lpc::nextvm::Chunk ch;
    ch.module_name = "self_check_call";
    ch.iconst = {2, 4};

    lpc::nextvm::FunctionProto add;
    add.name = "add2";
    add.arity = 2;
    add.nlocals = 2;
    add.code_start = 0;

    lpc::nextvm::FunctionProto mainf;
    mainf.name = "main";
    mainf.arity = 0;
    mainf.nlocals = 0;

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    auto emit_op = [&](lpc::nextvm::Op op) {
        ch.code.push_back(static_cast<std::uint8_t>(op));
    };

    emit_op(lpc::nextvm::Op::LoadLocal);
    emit_u16(0);
    emit_op(lpc::nextvm::Op::LoadLocal);
    emit_u16(1);
    emit_op(lpc::nextvm::Op::Add);
    emit_op(lpc::nextvm::Op::Return);
    add.code_end = static_cast<std::uint32_t>(ch.code.size());

    mainf.code_start = add.code_end;
    emit_op(lpc::nextvm::Op::LoadIConst);
    emit_u16(0);
    emit_op(lpc::nextvm::Op::LoadIConst);
    emit_u16(1);
    emit_op(lpc::nextvm::Op::CallValue);
    emit_u16(0);
    emit_u16(2);
    emit_op(lpc::nextvm::Op::Return);
    mainf.code_end = static_cast<std::uint32_t>(ch.code.size());

    ch.functions.push_back(add);
    ch.functions.push_back(mainf);

    lpc::nextvm::Vm vm;
    lpc::nextvm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    if (!e.ok()) {
        std::cerr << "[NextVM call] error: " << e.message << std::endl;
        return false;
    }
    lpc::nextvm::Value out = vm.last_result();
    if (!(out.tag == lpc::nextvm::ValueTag::Int64 && out.as.i64 == 6)) {
        std::cerr << "[NextVM call] bad result tag=" << static_cast<int>(out.tag)
                  << " value=" << out.as.i64 << std::endl;
    }
    return out.tag == lpc::nextvm::ValueTag::Int64 && out.as.i64 == 6;
}

static bool CheckNextVMVerifierRejectsBadJump()
{
    lpc::nextvm::Chunk ch;
    ch.module_name = "self_check_verify";
    lpc::nextvm::FunctionProto f;
    f.name = "main";
    f.code_start = 0;
    ch.functions.push_back(f);

    auto emit_u16 = [&](unsigned v) {
        ch.code.push_back(static_cast<std::uint8_t>(v & 0xff));
        ch.code.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };

    ch.code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Jump));
    emit_u16(200);
    ch.code.push_back(static_cast<std::uint8_t>(lpc::nextvm::Op::Return));
    ch.functions[0].code_end = static_cast<std::uint32_t>(ch.code.size());

    lpc::nextvm::Vm vm;
    lpc::nextvm::RuntimeError e = vm.LoadChunk(ch);
    if (!e.ok()) {
        return false;
    }
    e = vm.RunEntry("main");
    return !e.ok();
}

static bool CheckV1VerifierRejectsBadJump()
{
    char code[5] = {
        static_cast<char>(OpCode::op_goto),
        static_cast<char>(200),
        0,
        0,
        0,
    };
    object_proto_t proto;
    proto.instructions = code;
    proto.instruction_size = static_cast<lint32_t>(sizeof(code));

    vm::VerifyResult r = vm::VerifyV1Bytecode(proto);
    return !r.ok && r.message == "invalid jump target";
}

static bool CheckV1VerifierAcceptsEndJump()
{
    char code[5] = {
        static_cast<char>(OpCode::op_goto),
        5,
        0,
        0,
        0,
    };
    object_proto_t proto;
    proto.instructions = code;
    proto.instruction_size = static_cast<lint32_t>(sizeof(code));

    vm::VerifyResult r = vm::VerifyV1Bytecode(proto);
    return r.ok;
}

static bool CheckV1VerifierRejectsBadConstIndex()
{
    char code[4] = {
        static_cast<char>(OpCode::op_load_iconst),
        1,
        0,
        static_cast<char>(OpCode::op_return),
    };
    function_proto_t fn;
    fn.fromPC = 0;
    fn.toPC = static_cast<lint32_t>(sizeof(code));
    fn.nlocal = 0;

    object_proto_t proto;
    proto.instructions = code;
    proto.instruction_size = static_cast<lint32_t>(sizeof(code));
    proto.func_table = &fn;
    proto.nfunction = 1;
    proto.niconst = 1;

    vm::VerifyResult r = vm::VerifyV1Bytecode(proto);
    return !r.ok && r.message == "int const index out of range";
}

static bool CheckV1VerifierAcceptsGoodConstIndex()
{
    char code[4] = {
        static_cast<char>(OpCode::op_load_iconst),
        0,
        0,
        static_cast<char>(OpCode::op_return),
    };
    function_proto_t fn;
    fn.fromPC = 0;
    fn.toPC = static_cast<lint32_t>(sizeof(code));
    fn.nlocal = 0;

    object_proto_t proto;
    proto.instructions = code;
    proto.instruction_size = static_cast<lint32_t>(sizeof(code));
    proto.func_table = &fn;
    proto.nfunction = 1;
    proto.niconst = 1;

    vm::VerifyResult r = vm::VerifyV1Bytecode(proto);
    return r.ok;
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

static bool CheckClosureUpvalueWriteBarrier()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    function_proto_t proto;
    proto.nupvalue = 1;

    lpc_closure_t *closure = vm->get_alloc()->allocate_closure(&proto, nullptr);
    lpc_array_t *young = vm->get_alloc()->allocate_array(1);
    if (!closure || !young) {
        return false;
    }

    closure->header.generation = 1;
    young->header.generation = 0;

    lpc_value_t val;
    val.set_array(reinterpret_cast<lpc_gc_object_t *>(young));

    const luint64_t before_wb = vm->gc_write_barrier_count();
    const luint64_t before_remembered = vm->gc_remembered_set_size();
    closure->set(0, &val);

    return vm->gc_write_barrier_count() > before_wb &&
           vm->gc_remembered_set_size() > before_remembered;
}

static bool CheckMajorGcMarksRootChildren()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    lpc_array_t *parent = vm->get_alloc()->allocate_array(1);
    lpc_array_t *child = vm->get_alloc()->allocate_array(1);
    if (!parent || !child) {
        return false;
    }

    lpc_value_t child_val;
    child_val.set_array(reinterpret_cast<lpc_gc_object_t *>(child));
    parent->set(&child_val, 0);

    lpc_value_t key;
    key.set_int(4242);
    lpc_value_t parent_val;
    parent_val.set_array(reinterpret_cast<lpc_gc_object_t *>(parent));
    vm->get_object_cache()->set(&key, &parent_val);

    vm->get_gc()->gc();

    lpc_value_t *kept = parent->get(0);
    return kept && kept->is_array() && kept->get_gcobj() == reinterpret_cast<lpc_gc_object_t *>(child);
}

static bool CheckMinorGcKeepsRememberedClosureChild()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    function_proto_t proto;
    proto.nupvalue = 1;

    lpc_closure_t *closure = vm->get_alloc()->allocate_closure(&proto, nullptr);
    lpc_array_t *child = vm->get_alloc()->allocate_array(1);
    if (!closure || !child) {
        return false;
    }

    closure->header.generation = 1;
    child->header.generation = 0;

    lpc_value_t child_val;
    child_val.set_array(reinterpret_cast<lpc_gc_object_t *>(child));
    closure->set(0, &child_val);

    const luint64_t before_minor = vm->gc_minor_collect_count();
    vm->get_gc()->collect_minor();

    lpc_value_t *kept = closure->get(0);
    return vm->gc_minor_collect_count() == before_minor + 1 &&
           kept &&
           kept->is_array() &&
           kept->get_gcobj() == reinterpret_cast<lpc_gc_object_t *>(child) &&
           child->header.marked == 0;
}

static bool CheckMinorGcKeepsRememberedArrayChild()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    lpc_array_t *parent = vm->get_alloc()->allocate_array(1);
    lpc_array_t *child = vm->get_alloc()->allocate_array(1);
    if (!parent || !child) {
        return false;
    }

    parent->header.generation = 1;
    child->header.generation = 0;

    lpc_value_t child_val;
    child_val.set_array(reinterpret_cast<lpc_gc_object_t *>(child));
    parent->set(&child_val, 0);

    const luint64_t before_minor = vm->gc_minor_collect_count();
    vm->get_gc()->collect_minor();

    lpc_value_t *kept = parent->get(0);
    return vm->gc_minor_collect_count() == before_minor + 1 &&
           kept &&
           kept->is_array() &&
           kept->get_gcobj() == reinterpret_cast<lpc_gc_object_t *>(child) &&
           child->header.marked == 0;
}

static bool CheckMinorGcKeepsRememberedMappingChild()
{
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    if (!vm) {
        return false;
    }

    lpc_mapping_t *parent = vm->get_alloc()->allocate_mapping();
    lpc_array_t *child = vm->get_alloc()->allocate_array(1);
    if (!parent || !child) {
        return false;
    }

    parent->header.generation = 1;
    child->header.generation = 0;

    lpc_value_t key;
    key.set_int(7);
    lpc_value_t child_val;
    child_val.set_array(reinterpret_cast<lpc_gc_object_t *>(child));
    parent->set(&key, &child_val);

    const luint64_t before_minor = vm->gc_minor_collect_count();
    vm->get_gc()->collect_minor();

    lpc_value_t *kept = parent->get_value(&key);
    return vm->gc_minor_collect_count() == before_minor + 1 &&
           kept &&
           kept->is_array() &&
           kept->get_gcobj() == reinterpret_cast<lpc_gc_object_t *>(child) &&
           child->header.marked == 0;
}

int RunSelfChecks()
{
    int failed = 0;
    if (!CheckFrameInfoApi()) {
        std::cerr << "[vm-self-check] frame info api check failed" << std::endl;
        ++failed;
    }
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
    if (!CheckV1VerifierRejectsBadJump()) {
        std::cerr << "[vm-self-check] v1 verifier bad jump check failed" << std::endl;
        ++failed;
    }
    if (!CheckV1VerifierAcceptsEndJump()) {
        std::cerr << "[vm-self-check] v1 verifier end jump check failed" << std::endl;
        ++failed;
    }
    if (!CheckV1VerifierRejectsBadConstIndex()) {
        std::cerr << "[vm-self-check] v1 verifier bad const index check failed" << std::endl;
        ++failed;
    }
    if (!CheckV1VerifierAcceptsGoodConstIndex()) {
        std::cerr << "[vm-self-check] v1 verifier good const index check failed" << std::endl;
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
    if (!CheckClosureUpvalueWriteBarrier()) {
        std::cerr << "[vm-self-check] closure upvalue write barrier check failed" << std::endl;
        ++failed;
    }
    if (!CheckMajorGcMarksRootChildren()) {
        std::cerr << "[vm-self-check] major gc root child mark check failed" << std::endl;
        ++failed;
    }
    if (!CheckMinorGcKeepsRememberedClosureChild()) {
        std::cerr << "[vm-self-check] minor gc remembered closure child check failed" << std::endl;
        ++failed;
    }
    if (!CheckMinorGcKeepsRememberedArrayChild()) {
        std::cerr << "[vm-self-check] minor gc remembered array child check failed" << std::endl;
        ++failed;
    }
    if (!CheckMinorGcKeepsRememberedMappingChild()) {
        std::cerr << "[vm-self-check] minor gc remembered mapping child check failed" << std::endl;
        ++failed;
    }
    if (failed == 0) {
        std::cout << "[vm-self-check] PASS" << std::endl;
    }
    return failed;
}

} // namespace vmtest
} // namespace lpc
