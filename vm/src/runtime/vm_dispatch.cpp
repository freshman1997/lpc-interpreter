#include "vm/runtime/vm.h"
#include "vm/value/objref.h"
#include "vm/value/mapping.h"
#include "vm/value/lpc_array.h"
#include "vm/value/lpc_class.h"
#include "vm/value/lpc_closure.h"
#include "vm/value/hash.h"
#include "vm/runtime/vm_config.h"
#include "vm/runtime/efun.h"
#include "lpc/bytecode/opcode.h"
#include "vm/bytecode/verifier.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <array>
#include <stdexcept>

using namespace lpc::vm;

struct CatchSignal {};

struct CatchContext {
    bool active = false;
    std::uint32_t end_ip = 0;
    std::size_t stack_watermark = 0;
};

static std::uint16_t ReadU16(const std::vector<std::uint8_t> &code, std::uint32_t *ip) {
    const std::uint32_t i = *ip;
    std::uint16_t v = static_cast<std::uint16_t>(code[i + 1] << 8) | static_cast<std::uint16_t>(code[i]);
    *ip += 2;
    return v;
}

static std::int16_t ReadI16(const std::vector<std::uint8_t> &code, std::uint32_t *ip) {
    return static_cast<std::int16_t>(ReadU16(code, ip));
}

LPC_FORCEINLINE static std::uint16_t ReadU16Raw(const std::uint8_t *p, std::uint32_t *ip) {
    std::uint16_t v = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
    *ip += 2;
    return v;
}

RuntimeError Vm::RunEntry(const char *function_name) {
    RuntimeError verify = VerifyChunk(BoundChunk());
    if (!verify.ok()) {
        return verify;
    }

    RuntimeError init_err = RunInitCode();
    if (!init_err.ok()) {
        return init_err;
    }

    if (!function_name || !*function_name) {
        RuntimeError e;
        e.code = RuntimeErrorCode::InvalidOperand;
        e.message = "empty entry function";
        return e;
    }

    int func_id = FindBoundFunction(function_name);
    if (func_id < 0) {
        RuntimeError e;
        e.code = RuntimeErrorCode::NotCallable;
        e.message = std::string("entry function not found: ") + function_name;
        return e;
    }

    LpcObject master;
    master.module_name = BoundChunk().module_name;
    master.module_version_id = current_module_version_id_;
    master.blueprint = &BoundChunk();
    master.globals = BoundChunk().globals;
    master.destroyed = false;
    objects_.clear();
    object_free_.clear();
    object_marks_.clear();
    objects_.push_back(std::move(master));
    current_object_id_ = 1;

    const FunctionProto &f = BoundChunk().functions[func_id];
    Frame frame;
    frame.func_id = static_cast<std::uint32_t>(func_id);
    frame.ip = f.code_start;
    frame.base = 0;
    frame.stack_top = 0;
    frame.object_id = 1;
    frame.module_version_id = current_module_version_id_;
    frame.module_name = current_module_name_;
    if (frame.module_version_id != 0) {
        RuntimeError pin_err = hot_reload_manager_.PinVersion(current_module_name_, frame.module_version_id);
        if (!pin_err.ok()) {
            return pin_err;
        }
        frame.version_pinned = true;
    }
    frames_.clear();
    frames_.reserve(kMaxCallFrames);
    frames_.push_back(std::move(frame));
    value_stack_.clear();
    value_stack_.reserve(kMaxStackSize);
    const std::size_t local_slots = static_cast<std::size_t>(f.nlocals);
    value_stack_.resize(local_slots, Value::Nil());
    frames_.back().stack_top = static_cast<std::uint32_t>(value_stack_.size());

    arrays_.clear();
    mappings_.clear();
    class_fields_.clear();
    class_template_ids_.clear();
    array_free_.clear();
    mapping_free_.clear();
    class_free_.clear();
    closure_free_.clear();
    object_free_.clear();
    array_slot_free_.clear();
    mapping_slot_free_.clear();
    class_slot_free_.clear();
    closure_slot_free_.clear();
    object_slot_free_.clear();

#define LPC_PUSH(v)    do { value_stack_.push_back(v); ++sp; } while(0)
#define LPC_POP()      ({ Value _v = sp[-1]; value_stack_.pop_back(); --sp; _v; })
#define LPC_TOP()      sp[-1]
#define LPC_PEEK(n)    sp[-1-(n)]
#define LPC_STACK_SZ() static_cast<std::size_t>(sp - value_stack_.data())
#define LPC_STACK_SIZE() LPC_STACK_SZ()
#define LPC_SYNC_SP()  do { sp = value_stack_.data() + value_stack_.size(); } while(0)
#define LPC_LOAD_SP()  do { sp = value_stack_.data() + value_stack_.size(); } while(0)
#define LPC_DISCARD()  do { value_stack_.pop_back(); --sp; } while(0)

#if LPC_ENABLE_COMPUTED_GOTO
    static const void *kComputedGotoSkeleton[] = {
        &&lpc_cg_label_load_local,
        &&lpc_cg_label_store_local,
        &&lpc_cg_label_load_iconst,
        &&lpc_cg_label_add,
        &&lpc_cg_label_return,
    };
    (void)kComputedGotoSkeleton;
lpc_cg_label_load_local:
lpc_cg_label_store_local:
lpc_cg_label_load_iconst:
lpc_cg_label_add:
lpc_cg_label_return:
    ;
#endif

    CatchContext catch_ctx;
    BeginProfile();
    const bool instruction_debug_checks = debug_checks_enabled_ || debugger_.active();

    Value *sp = value_stack_.data() + value_stack_.size();

    auto push_frame = [&](std::uint32_t func_id,
                          std::uint32_t ip,
                          std::uint32_t base,
                          std::uint32_t stack_top,
                          std::uint32_t object_id,
                          std::uint64_t module_version_id,
                          const std::string &module_name,
                          bool version_pinned_by_caller,
                          std::uint32_t closure_slot) -> RuntimeError {
        Frame next;
        next.func_id = func_id;
        next.ip = ip;
        next.base = base;
        next.stack_top = stack_top;
        next.object_id = object_id;
        next.module_version_id = module_version_id;
        next.module_name = module_name;
        next.closure_slot = closure_slot;
        next.version_pinned = false;
        if (next.module_version_id != 0 && !version_pinned_by_caller) {
            RuntimeError pin_err = hot_reload_manager_.PinVersion(next.module_name, next.module_version_id);
            if (!pin_err.ok()) {
                if (catch_ctx.active) throw CatchSignal();
                return pin_err;
            }
            next.version_pinned = true;
        }
        frames_.push_back(std::move(next));
        return RuntimeError::Ok();
    };

    auto extend_call_locals = [&](const FunctionProto &callee, std::uint16_t call_argc) {
        const std::size_t local_slots = static_cast<std::size_t>(
            callee.nlocals > call_argc ? (callee.nlocals - call_argc) : 0);
        if (local_slots != 0) {
            value_stack_.resize(value_stack_.size() + local_slots, Value::Nil());
            LPC_LOAD_SP();
        }
    };

    auto ensure_call_stack_room = [&]() -> RuntimeError {
        if (frames_.size() >= kMaxCallFrames) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InternalError;
            e.message = "call stack limit exceeded";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        return RuntimeError::Ok();
    };

    while (!frames_.empty()) {
        if (LPC_UNLIKELY(alloc_count_ >= gc_threshold_)) {
            LPC_SYNC_SP();
            CollectGarbage();
            gc_threshold_ = std::max(gc_threshold_,
                (arrays_.size() + mappings_.size() + class_fields_.size() +
                 string_heap_.size() + closures_.size() + objects_.size()) * 2 + kGcThresholdPadding);
            LPC_LOAD_SP();
        }
        Frame *fp = &frames_.back();
        if (LPC_UNLIKELY(bound_module_version_id_ != fp->module_version_id)) {
            LPC_SYNC_SP();
            RuntimeError bind_err = BindExecutionVersion(fp->module_name, fp->module_version_id);
            if (!bind_err.ok()) {
                if (catch_ctx.active) throw CatchSignal();
                return bind_err;
            }
            LPC_LOAD_SP();
            fp = &frames_.back();
        }
        const auto *code = BoundChunk().code.data();
        const auto code_size = BoundChunk().code.size();
        const auto *iconst_values = BoundIConst().data();
        const auto iconst_size = BoundIConst().size();
        const auto *fconst_values = BoundFConst().data();
        const auto fconst_size = BoundFConst().size();
        const auto *sconst_values = BoundSConst().data();
        const auto sconst_size = BoundSConst().size();
        current_object_id_ = fp->object_id;
        const FunctionProto &curf = BoundChunk().functions[fp->func_id];
        try {
        if (LPC_UNLIKELY(instruction_debug_checks && fp->ip >= code_size)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "instruction pointer out of range";
            e.function = curf.name;
            e.pc = static_cast<int>(fp->ip);
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }

        if (LPC_UNLIKELY(instruction_debug_checks && debugger_.active() && debugger_.ShouldBreak(fp->ip, static_cast<std::uint32_t>(frames_.size()), BoundChunk(), frames_, value_stack_))) {
            debugger_.ClearStepOnBreak(static_cast<std::uint32_t>(frames_.size()));
            if (debug_hook_) {
                RuntimeError hook_err = debug_hook_(fp->ip);
                if (!hook_err.ok()) {
                    return hook_err;
                }
            }
        }

        Op op = static_cast<Op>(code[fp->ip++]);
        RecordOpcode(static_cast<std::uint8_t>(op));

        bool fast_handled = false;
#if LPC_ENABLE_COMPUTED_GOTO
        static const void *kComputedGotoDispatch[256] = {};
        static bool kComputedGotoDispatchInit = false;
        if (!kComputedGotoDispatchInit) {
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::Pop)] = &&lpc_cg_pop;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::Dup)] = &&lpc_cg_dup;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::LoadLocal)] = &&lpc_cg_load_local;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::StoreLocal)] = &&lpc_cg_store_local;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::LoadIConst)] = &&lpc_cg_load_iconst;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::LoadFConst)] = &&lpc_cg_load_fconst;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::LoadSConst)] = &&lpc_cg_load_sconst;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::Add)] = &&lpc_cg_add;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::Return)] = &&lpc_cg_return;
            kComputedGotoDispatchInit = true;
        }
        const void *cg_target = kComputedGotoDispatch[static_cast<std::uint8_t>(op)];
        if (cg_target != nullptr) {
            goto *cg_target;
        }
        goto lpc_dispatch_fallback;
lpc_cg_pop:
        LPC_DISCARD();
        goto lpc_post_dispatch;
lpc_cg_dup:
        LPC_PUSH(LPC_TOP());
        goto lpc_post_dispatch;
lpc_cg_load_local:
        if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "truncated LoadLocal operand";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(value_stack_[fp->base + idx]);
        }
        goto lpc_post_dispatch;
lpc_cg_store_local:
        if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "truncated StoreLocal operand";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SZ() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreLocal requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            value_stack_[fp->base + idx] = LPC_POP();
        }
        goto lpc_post_dispatch;
lpc_cg_add:
#ifdef NDEBUG
        if (LPC_UNLIKELY(LPC_STACK_SZ() < fp->stack_top + 2)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::StackUnderflow;
            e.message = "Add requires two operands";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            Value rhs = LPC_POP();
            Value &lhs = LPC_PEEK(0);
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                lhs = MakeI64(lhs.AsI64() + rhs.AsI64());
                goto lpc_post_dispatch;
            }
            LPC_PUSH(rhs);
            goto lpc_add_slow;
        }
#else
        if (LPC_UNLIKELY(LPC_STACK_SZ() < fp->stack_top + 2)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::StackUnderflow;
            e.message = "Add requires two operands";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            const Value rhs = LPC_TOP();
            Value &lhs = LPC_PEEK(1);
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                lhs = MakeI64(lhs.AsI64() + rhs.AsI64());
                LPC_DISCARD();
                goto lpc_post_dispatch;
            }
            goto lpc_add_slow;
        }
#endif
lpc_cg_return:
        {
            const std::uint32_t base = fp->base;
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                last_result_ = Value::Nil();
            } else {
                last_result_ = LPC_POP();
            }

            std::string returning_module_name = std::move(fp->module_name);
            const std::uint64_t returning_version_id = fp->module_version_id;
            const bool returning_version_pinned = fp->version_pinned;
            LPC_SYNC_SP();
            frames_.pop_back();
            if (returning_version_pinned && returning_version_id != 0) {
                RuntimeError unpin_err = hot_reload_manager_.UnpinVersion(returning_module_name, returning_version_id);
                if (!unpin_err.ok()) {
                    if (catch_ctx.active) throw CatchSignal();
                    return unpin_err;
                }
            }

            if (value_stack_.size() > base) {
                value_stack_.resize(base);
            }
            LPC_LOAD_SP();
            if (!frames_.empty()) {
                LPC_PUSH(last_result_);
                fp = &frames_.back();
            }
        }
        goto lpc_post_dispatch;
lpc_cg_load_iconst:
        if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "truncated LoadIConst operand";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= iconst_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "iconst index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(iconst_values[idx]);
        }
        goto lpc_post_dispatch;
lpc_cg_load_fconst:
        if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "truncated LoadFConst operand";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= fconst_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "fconst index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(fconst_values[idx]);
        }
        goto lpc_post_dispatch;
lpc_cg_load_sconst:
        if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOperand;
            e.message = "truncated LoadSConst operand";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }
        {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= sconst_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "sconst index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(sconst_values[idx]);
        }
        goto lpc_post_dispatch;
#endif

lpc_dispatch_fallback:
        fast_handled = false;
        // 中文说明：这里是 Windows/Release 当前使用最多的快速分发链。
        // 优先处理局部变量、整数算术、跳转和 superinstruction；复杂类型仍落回下面的大 switch。
        if (LPC_LIKELY(op == Op::LoadLocal)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            LPC_PUSH(value_stack_[fp->base + idx]);
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated LoadLocal operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            LPC_PUSH(value_stack_[fp->base + idx]);
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::LoadLocalDec || op == Op::LoadLocalSubIConst || op == Op::LoadLocalAddIConst)) {
            const std::uint32_t operand_size = op == Op::LoadLocalDec ? 2u : 4u;
            if (LPC_UNLIKELY(fp->ip + operand_size > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated local expression operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += operand_size;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t const_idx = (op == Op::LoadLocalSubIConst || op == Op::LoadLocalAddIConst)
                ? static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8)
                : 0;
#ifndef NDEBUG
            if (LPC_UNLIKELY(local_idx >= curf.nlocals ||
                ((op == Op::LoadLocalSubIConst || op == Op::LoadLocalAddIConst) && const_idx >= iconst_size))) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local expression operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value rhs = (op == Op::LoadLocalSubIConst || op == Op::LoadLocalAddIConst) ? iconst_values[const_idx] : MakeI64(1);
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                LPC_PUSH(MakeI64(op == Op::LoadLocalAddIConst ? lhs.AsI64() + rhs.AsI64() : lhs.AsI64() - rhs.AsI64()));
            } else if (op == Op::LoadLocalAddIConst && LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string result;
                result.reserve(sv_a.size() + sv_b.size());
                result.append(sv_a);
                result.append(sv_b);
                LPC_PUSH(InternString(result));
                LPC_LOAD_SP();
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : (rhs.IsNil() ? 0.0 : static_cast<double>(GetI64(rhs)));
                LPC_PUSH(Value::FromF64(op == Op::LoadLocalAddIConst ? a + b : a - b));
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                LPC_PUSH(MakeI64(op == Op::LoadLocalAddIConst ? a + b : a - b));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = op == Op::LoadLocalAddIConst ? "Add unsupported types" : "Sub unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::StoreLocal)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            value_stack_[fp->base + idx] = LPC_POP();
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated StoreLocal operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SZ() <= fp->stack_top)) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "StoreLocal requires one operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + idx] = LPC_POP();
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::IncLocal || op == Op::DecLocal)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            Value &slot = value_stack_[fp->base + idx];
            if (LPC_LIKELY(slot.Tag() == ValueTag::Int64)) {
                slot = MakeI64(slot.AsI64() + (op == Op::IncLocal ? 1 : -1));
            } else if (slot.IsNil()) {
                slot = Value::FromI64(op == Op::IncLocal ? 1 : -1);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "IncLocal/DecLocal requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated IncLocal/DecLocal operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            Value &slot = value_stack_[fp->base + idx];
            if (slot.Tag() == ValueTag::Int64) {
                slot = MakeI64(GetI64(slot) + (op == Op::IncLocal ? 1 : -1));
            } else if (slot.IsNil()) {
                slot = Value::FromI64(op == Op::IncLocal ? 1 : -1);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "IncLocal/DecLocal requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::IncLocalAndJump)) {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated IncLocalAndJump operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8));
#ifndef NDEBUG
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            Value &slot = value_stack_[fp->base + idx];
            if (LPC_LIKELY(slot.Tag() == ValueTag::Int64)) {
                slot = MakeI64(slot.AsI64() + 1);
            } else if (slot.IsNil()) {
                slot = Value::FromI64(1);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "IncLocalAndJump requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
#ifndef NDEBUG
            if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "jump target out of function range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            fp->ip = static_cast<std::uint32_t>(next_ip);
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::LoadIConst)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            LPC_PUSH(iconst_values[idx]);
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated LoadIConst operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "iconst index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            LPC_PUSH(iconst_values[idx]);
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::Add)) {
#ifdef NDEBUG
            Value rhs = LPC_POP();
            Value &lhs = LPC_PEEK(0);
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                lhs = MakeI64(lhs.AsI64() + rhs.AsI64());
                fast_handled = true;
            } else {
                LPC_PUSH(rhs);
                fast_handled = false;
                goto lpc_add_slow;
            }
#else
            if (LPC_UNLIKELY(LPC_STACK_SZ() < fp->stack_top + 2)) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "Add requires two operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const Value rhs = LPC_TOP();
            Value &lhs = LPC_PEEK(1);
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                lhs = MakeI64(lhs.AsI64() + rhs.AsI64());
                LPC_DISCARD();
                fast_handled = true;
            } else {
                fast_handled = false;
            }
#endif
        } else if (LPC_LIKELY(op == Op::Sub)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                    lhs = MakeI64(lhs.AsI64() - rhs.AsI64());
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Mul)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                    lhs = MakeI64(lhs.AsI64() * rhs.AsI64());
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Lt)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                    lhs = Value::FromI64((lhs.AsI64() < rhs.AsI64()) ? 1 : 0);
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Lte)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                    lhs = Value::FromI64((lhs.AsI64() <= rhs.AsI64()) ? 1 : 0);
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Gt)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                    lhs = Value::FromI64((lhs.AsI64() > rhs.AsI64()) ? 1 : 0);
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Gte)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                    lhs = Value::FromI64((lhs.AsI64() >= rhs.AsI64()) ? 1 : 0);
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::AddLocalLocalToLocal || op == Op::AddLocalIConstToLocal || op == Op::SubLocalIConstToLocal)) {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated local arithmetic operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals ||
                (op == Op::AddLocalLocalToLocal ? rhs_idx >= curf.nlocals : rhs_idx >= iconst_size))) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = op == Op::AddLocalLocalToLocal ? value_stack_[fp->base + rhs_idx] : iconst_values[rhs_idx];
            const bool is_sub = op == Op::SubLocalIConstToLocal;
            Value result = Value::Nil();
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                result = MakeI64(is_sub ? lhs.AsI64() - rhs.AsI64() : lhs.AsI64() + rhs.AsI64());
            } else if (!is_sub && LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
                LPC_LOAD_SP();
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : (rhs.IsNil() ? 0.0 : static_cast<double>(GetI64(rhs)));
                result = Value::FromF64(is_sub ? a - b : a + b);
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                result = MakeI64(is_sub ? a - b : a + b);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = is_sub ? "Sub unsupported types" : "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::AddLocalIndexIConstToLocal || op == Op::AddLocalIndexLocalToLocal)) {
            if (LPC_UNLIKELY(fp->ip + 8 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated AddLocalIndex operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 8;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t container_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            const std::uint16_t key_idx = static_cast<std::uint16_t>(p[6]) | (static_cast<std::uint16_t>(p[7]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || container_idx >= curf.nlocals ||
                (op == Op::AddLocalIndexIConstToLocal ? key_idx >= iconst_size : key_idx >= curf.nlocals))) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index accumulation operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value container = value_stack_[fp->base + container_idx];
            const Value key = op == Op::AddLocalIndexIConstToLocal ? iconst_values[key_idx] : value_stack_[fp->base + key_idx];
            Value elem = Value::Nil();

            if (container.IsObjRef() && key.Tag() == ValueTag::Int64) {
                const std::int64_t key_i = GetI64(key);
                if (IsStringObjRef(container)) {
                    LPC_SYNC_SP();
                    std::string s = ResolveObjRefStringOnly(container);
                    if (key_i < 0 || static_cast<std::size_t>(key_i) >= s.size()) {
                        RuntimeError e; e.code = RuntimeErrorCode::BoundsError; e.message = "string index out of range";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    std::string ch(1, s[static_cast<std::size_t>(key_i)]);
                    elem = InternString(ch);
                    LPC_LOAD_SP();
                } else {
                    const std::size_t arr_id = DecodeArrayId(container);
                    if (arr_id > 0 && arr_id <= arrays_.size()) {
                        LpcArray &arr = arrays_[arr_id - 1];
                        if (key_i < 0 || static_cast<std::size_t>(key_i) >= arr.Size()) {
                            RuntimeError e; e.code = RuntimeErrorCode::BoundsError; e.message = "array index out of range";
                            if (catch_ctx.active) throw CatchSignal(); return e;
                        }
                        elem = arr.At(static_cast<std::size_t>(key_i));
                    } else {
                        const std::size_t map_id = DecodeMappingId(container);
                        if (map_id > 0 && map_id <= mappings_.size()) {
                            const Value *found = mappings_[map_id - 1].Find(key);
                            elem = found ? *found : Value::Nil();
                        } else {
                            RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "array handle out of range";
                            if (catch_ctx.active) throw CatchSignal(); return e;
                        }
                    }
                }
            } else if (container.IsObjRef()) {
                const std::size_t map_id = DecodeMappingId(container);
                if (map_id > 0 && map_id <= mappings_.size()) {
                    const Value *found = mappings_[map_id - 1].Find(key);
                    elem = found ? *found : Value::Nil();
                } else {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "Index: container is not array, string, or mapping";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
            } else if (!container.IsNil()) {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "Index expects array/string/mapping + key";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }

            Value result = Value::Nil();
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && elem.Tag() == ValueTag::Int64)) {
                result = MakeI64(lhs.AsI64() + elem.AsI64());
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || elem.IsObjRef())) {
                LPC_SYNC_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(elem, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
                LPC_LOAD_SP();
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || elem.IsFloat64())) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = elem.IsFloat64() ? elem.AsF64() : (elem.IsNil() ? 0.0 : static_cast<double>(GetI64(elem)));
                result = Value::FromF64(a + b);
            } else if (LPC_UNLIKELY(lhs.IsNil() || elem.IsNil())) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = elem.IsNil() ? 0 : GetI64(elem);
                result = MakeI64(a + b);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::AddLocalLocalIncJumpIfLocalLt)) {
            if (LPC_UNLIKELY(fp->ip + 14 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated AddLocalLocalIncJumpIfLocalLt operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 14;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            const std::uint16_t inc_idx = static_cast<std::uint16_t>(p[6]) | (static_cast<std::uint16_t>(p[7]) << 8);
            const std::uint16_t cmp_lhs_idx = static_cast<std::uint16_t>(p[8]) | (static_cast<std::uint16_t>(p[9]) << 8);
            const std::uint16_t cmp_rhs_idx = static_cast<std::uint16_t>(p[10]) | (static_cast<std::uint16_t>(p[11]) << 8);
            const std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[12]) | (static_cast<std::uint16_t>(p[13]) << 8));
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= curf.nlocals ||
                inc_idx >= curf.nlocals || cmp_lhs_idx >= curf.nlocals || cmp_rhs_idx >= curf.nlocals)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "loop-tail local operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = value_stack_[fp->base + rhs_idx];
            Value result = Value::Nil();
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                result = MakeI64(lhs.AsI64() + rhs.AsI64());
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
                LPC_LOAD_SP();
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : (rhs.IsNil() ? 0.0 : static_cast<double>(GetI64(rhs)));
                result = Value::FromF64(a + b);
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                result = MakeI64(a + b);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;

            Value &inc_slot = value_stack_[fp->base + inc_idx];
            if (LPC_LIKELY(inc_slot.Tag() == ValueTag::Int64)) {
                inc_slot = MakeI64(inc_slot.AsI64() + 1);
            } else if (inc_slot.IsNil()) {
                inc_slot = Value::FromI64(1);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "IncLocal requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }

            const Value cmp_lhs = value_stack_[fp->base + cmp_lhs_idx];
            const Value cmp_rhs = value_stack_[fp->base + cmp_rhs_idx];
            bool cond = false;
            if (LPC_LIKELY(cmp_lhs.Tag() == ValueTag::Int64 && cmp_rhs.Tag() == ValueTag::Int64)) {
                cond = cmp_lhs.AsI64() < cmp_rhs.AsI64();
            } else if (LPC_UNLIKELY(cmp_lhs.IsFloat64() || cmp_rhs.IsFloat64())) {
                if (LPC_UNLIKELY(!(cmp_lhs.IsFloat64() || cmp_lhs.Tag() == ValueTag::Int64) ||
                                 !(cmp_rhs.IsFloat64() || cmp_rhs.Tag() == ValueTag::Int64))) {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                const double a = cmp_lhs.IsFloat64() ? cmp_lhs.AsF64() : static_cast<double>(GetI64(cmp_lhs));
                const double b = cmp_rhs.IsFloat64() ? cmp_rhs.AsF64() : static_cast<double>(GetI64(cmp_rhs));
                cond = a < b;
            } else if (LPC_UNLIKELY(cmp_lhs.IsNil() || cmp_rhs.IsNil())) {
                cond = false;
            } else if (LPC_UNLIKELY(cmp_lhs.IsObjRef() || cmp_rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string a = ResolveString(cmp_lhs);
                std::string b = ResolveString(cmp_rhs);
                LPC_LOAD_SP();
                cond = a < b;
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (cond) {
                const std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
#ifndef NDEBUG
                if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                    RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "jump target out of function range";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
#endif
                fp->ip = static_cast<std::uint32_t>(next_ip);
            }
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::JumpIfLocalLtFalse || op == Op::JumpIfLocalIConstLteFalse)) {
            // 合并“取局部变量 -> 比较 -> 条件跳转”，主要服务循环条件和递归边界判断。
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated local compare jump operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8));
#ifndef NDEBUG
            if (LPC_UNLIKELY(lhs_idx >= curf.nlocals ||
                (op == Op::JumpIfLocalLtFalse ? rhs_idx >= curf.nlocals : rhs_idx >= iconst_size))) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = op == Op::JumpIfLocalLtFalse
                ? value_stack_[fp->base + rhs_idx]
                : iconst_values[rhs_idx];
            bool cond = false;
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                cond = op == Op::JumpIfLocalLtFalse
                    ? lhs.AsI64() < rhs.AsI64()
                    : lhs.AsI64() <= rhs.AsI64();
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                if (LPC_UNLIKELY(!(lhs.IsFloat64() || lhs.Tag() == ValueTag::Int64) ||
                                 !(rhs.IsFloat64() || rhs.Tag() == ValueTag::Int64))) {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                const double a = lhs.IsFloat64() ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                cond = op == Op::JumpIfLocalLtFalse ? a < b : a <= b;
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                cond = false;
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string a = ResolveString(lhs);
                std::string b = ResolveString(rhs);
                LPC_LOAD_SP();
                cond = op == Op::JumpIfLocalLtFalse ? a < b : a <= b;
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (!cond) {
                const std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
#ifndef NDEBUG
                if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                    RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "jump target out of function range";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
#endif
                fp->ip = static_cast<std::uint32_t>(next_ip);
            }
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::Jump)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated Jump operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
            if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "jump target out of function range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            fp->ip = static_cast<std::uint32_t>(next_ip);
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::JumpIfFalse)) {
#ifdef NDEBUG
            Value cond = LPC_POP();
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            if (!IsTruthy(cond)) {
                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
            }
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated JumpIfFalse operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SZ() <= fp->stack_top)) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "JumpIfFalse requires condition";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            Value cond = LPC_POP();
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            if (!IsTruthy(cond)) {
                std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
                if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                    RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "jump target out of function range";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                fp->ip = static_cast<std::uint32_t>(next_ip);
            }
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::JumpIfTrue)) {
#ifdef NDEBUG
            Value cond = LPC_POP();
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            if (IsTruthy(cond)) {
                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
            }
#else
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated JumpIfTrue operand";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SZ() <= fp->stack_top)) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "JumpIfTrue requires condition";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            Value cond = LPC_POP();
            const std::uint8_t *p = code + fp->ip; fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            if (IsTruthy(cond)) {
                std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
                if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                    RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "jump target out of function range";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                fp->ip = static_cast<std::uint32_t>(next_ip);
            }
#endif
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::Dup)) {
            LPC_PUSH(LPC_TOP());
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::Pop)) {
            LPC_DISCARD();
            fast_handled = true;
        }

        if (!fast_handled) {
#if LPC_ENABLE_COMPUTED_GOTO
lpc_cg_fallback:
#endif
        switch (op) {

        lpc_add_slow:
        case Op::Add: {
            if (LPC_STACK_SZ() < fp->stack_top + 2) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "Add requires two operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                LPC_PUSH(MakeI64(lhs.AsI64() + rhs.AsI64()));
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string result;
                result.reserve(sv_a.size() + sv_b.size());
                result.append(sv_a);
                result.append(sv_b);
                LPC_PUSH(InternString(result));
                LPC_LOAD_SP();
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                double a = (lhs.IsFloat64()) ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                double b = (rhs.IsFloat64()) ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                LPC_PUSH(Value::FromF64(a + b));
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                std::int64_t a = (lhs.IsNil()) ? 0 : GetI64(lhs);
                std::int64_t b = (rhs.IsNil()) ? 0 : GetI64(rhs);
                LPC_PUSH(MakeI64(a + b));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            break;
        }

        case Op::LoadFConst: {
#ifndef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadFConst operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= fconst_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "fconst index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
#else
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
#endif
            LPC_PUSH(fconst_values[idx]);
            break;
        }

        case Op::LoadSConst: {
#ifndef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadSConst operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= sconst_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "sconst index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
#else
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
#endif
            LPC_PUSH(sconst_values[idx]);
            break;
        }

        case Op::Sub: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Sub requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                double a = (lhs.IsFloat64()) ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                double b = (rhs.IsFloat64()) ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                Value out;
                out = Value::FromF64(a - b);
                LPC_PUSH(out);
            } else if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                Value out;
                out = MakeI64(GetI64(lhs) - GetI64(rhs));
                LPC_PUSH(out);
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                std::int64_t a = (lhs.IsNil()) ? 0 : GetI64(lhs);
                std::int64_t b = (rhs.IsNil()) ? 0 : GetI64(rhs);
                Value out;
                out = MakeI64(a - b);
                LPC_PUSH(out);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Sub unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::Mul: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Mul requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                double a = (lhs.IsFloat64()) ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                double b = (rhs.IsFloat64()) ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                Value out;
                out = Value::FromF64(a * b);
                LPC_PUSH(out);
            } else if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                Value out;
                out = MakeI64(GetI64(lhs) * GetI64(rhs));
                LPC_PUSH(out);
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                std::int64_t a = (lhs.IsNil()) ? 0 : GetI64(lhs);
                std::int64_t b = (rhs.IsNil()) ? 0 : GetI64(rhs);
                Value out;
                out = MakeI64(a * b);
                LPC_PUSH(out);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Mul unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::Div: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Div requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                double a = (lhs.IsFloat64()) ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                double b = (rhs.IsFloat64()) ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                if (b == 0.0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "division by zero";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value out;
                out = Value::FromF64(a / b);
                LPC_PUSH(out);
            } else if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                if (GetI64(rhs) == 0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "division by zero";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value out;
                out = MakeI64(GetI64(lhs) / GetI64(rhs));
                LPC_PUSH(out);
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                std::int64_t a = (lhs.IsNil()) ? 0 : GetI64(lhs);
                std::int64_t b = (rhs.IsNil()) ? 0 : GetI64(rhs);
                if (b == 0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "division by zero";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value out;
                out = MakeI64(a / b);
                LPC_PUSH(out);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Div unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::Mod: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Mod requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64) {
                if (GetI64(rhs) == 0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "mod by zero";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value out;
                out = MakeI64(GetI64(lhs) % GetI64(rhs));
                LPC_PUSH(out);
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                std::int64_t a = (lhs.IsNil()) ? 0 : GetI64(lhs);
                std::int64_t b = (rhs.IsNil()) ? 0 : GetI64(rhs);
                if (b == 0) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "mod by zero";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value out;
                out = MakeI64(a % b);
                LPC_PUSH(out);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Mod requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::Neg: {
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Neg requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value v = LPC_POP();
            Value out;
            if (v.Tag() == ValueTag::Int64) {
                out = MakeI64(-GetI64(v));
            } else if (v.IsFloat64()) {
                out = Value::FromF64(-v.AsF64());
            } else if (v.IsNil()) {
                out = Value::FromI64(0);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Neg requires numeric type";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(out);
            break;
        }

        case Op::Inc: {
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Inc requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value v = LPC_POP();
            if (v.Tag() == ValueTag::Int64) {
                Value out;
                out = MakeI64(GetI64(v) + 1);
                LPC_PUSH(out);
            } else if (v.IsNil()) {
                Value out;
                out = Value::FromI64(1);
                LPC_PUSH(out);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Inc requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::Dec: {
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Dec requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value v = LPC_POP();
            if (v.Tag() == ValueTag::Int64) {
                Value out;
                out = MakeI64(GetI64(v) - 1);
                LPC_PUSH(out);
            } else if (v.IsNil()) {
                Value out;
                out = Value::FromI64(-1);
                LPC_PUSH(out);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Dec requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::Shl: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Shl requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (lhs.Tag() != ValueTag::Int64 || rhs.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Shl requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value out;
            out = MakeI64(GetI64(lhs) << GetI64(rhs));
            LPC_PUSH(out);
            break;
        }

        case Op::Shr: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Shr requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (lhs.Tag() != ValueTag::Int64 || rhs.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Shr requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value out;
            out = MakeI64(GetI64(lhs) >> GetI64(rhs));
            LPC_PUSH(out);
            break;
        }

        case Op::BitAnd: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "BitAnd requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (lhs.Tag() != ValueTag::Int64 || rhs.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "BitAnd requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value out;
            out = MakeI64(GetI64(lhs) & GetI64(rhs));
            LPC_PUSH(out);
            break;
        }

        case Op::BitOr: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "BitOr requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (lhs.Tag() != ValueTag::Int64 || rhs.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "BitOr requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value out;
            out = MakeI64(GetI64(lhs) | GetI64(rhs));
            LPC_PUSH(out);
            break;
        }

        case Op::BitXor: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "BitXor requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (lhs.Tag() != ValueTag::Int64 || rhs.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "BitXor requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value out;
            out = MakeI64(GetI64(lhs) ^ GetI64(rhs));
            LPC_PUSH(out);
            break;
        }

        case Op::BitNot: {
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "BitNot requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value v = LPC_POP();
            if (v.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "BitNot requires Int64";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value out;
            out = MakeI64(~GetI64(v));
            LPC_PUSH(out);
            break;
        }

        case Op::Eq:
        case Op::Neq:
        case Op::Gt:
        case Op::Gte:
        case Op::Lt:
        case Op::Lte: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "compare op requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            Value out;
            out = Value::FromI64(0);
            if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                double a = (lhs.IsFloat64()) ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                double b = (rhs.IsFloat64()) ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                switch (op) {
                case Op::Eq:  out = Value::FromI64((a == b) ? 1 : 0); break;
                case Op::Neq: out = Value::FromI64((a != b) ? 1 : 0); break;
                case Op::Gt:  out = Value::FromI64((a > b) ? 1 : 0); break;
                case Op::Gte: out = Value::FromI64((a >= b) ? 1 : 0); break;
                case Op::Lt:  out = Value::FromI64((a < b) ? 1 : 0); break;
                case Op::Lte: out = Value::FromI64((a <= b) ? 1 : 0); break;
                default: break;
                }
            } else if (LPC_LIKELY(lhs.Tag() == ValueTag::Int64 && rhs.Tag() == ValueTag::Int64)) {
                switch (op) {
                case Op::Eq:  out = Value::FromI64((GetI64(lhs) == GetI64(rhs)) ? 1 : 0); break;
                case Op::Neq: out = Value::FromI64((GetI64(lhs) != GetI64(rhs)) ? 1 : 0); break;
                case Op::Gt:  out = Value::FromI64((GetI64(lhs) > GetI64(rhs)) ? 1 : 0); break;
                case Op::Gte: out = Value::FromI64((GetI64(lhs) >= GetI64(rhs)) ? 1 : 0); break;
                case Op::Lt:  out = Value::FromI64((GetI64(lhs) < GetI64(rhs)) ? 1 : 0); break;
                case Op::Lte: out = Value::FromI64((GetI64(lhs) <= GetI64(rhs)) ? 1 : 0); break;
                default: break;
                }
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                bool eq = (lhs.IsNil() && rhs.IsNil());
                switch (op) {
                case Op::Eq:  out = Value::FromI64(eq ? 1 : 0); break;
                case Op::Neq: out = Value::FromI64(eq ? 0 : 1); break;
                default:
                    out = Value::FromI64(0);
                    break;
                }
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_SYNC_SP();
                std::string a = ResolveString(lhs);
                std::string b = ResolveString(rhs);
                LPC_LOAD_SP();
                switch (op) {
                case Op::Eq:  out = Value::FromI64((a == b) ? 1 : 0); break;
                case Op::Neq: out = Value::FromI64((a != b) ? 1 : 0); break;
                case Op::Gt:  out = Value::FromI64((a > b) ? 1 : 0); break;
                case Op::Gte: out = Value::FromI64((a >= b) ? 1 : 0); break;
                case Op::Lt:  out = Value::FromI64((a < b) ? 1 : 0); break;
                case Op::Lte: out = Value::FromI64((a <= b) ? 1 : 0); break;
                default: break;
                }
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "compare op unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(out);
            break;
        }

        case Op::LogicAnd: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "LogicAnd requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            Value out;
            out = Value::FromI64((IsTruthy(lhs) && IsTruthy(rhs)) ? 1 : 0);
            LPC_PUSH(out);
            break;
        }

        case Op::LogicOr: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "LogicOr requires two operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            Value out;
            out = Value::FromI64((IsTruthy(lhs) || IsTruthy(rhs)) ? 1 : 0);
            LPC_PUSH(out);
            break;
        }

        case Op::LogicNot: {
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "LogicNot requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value v = LPC_POP();
            Value out;
            out = Value::FromI64(IsTruthy(v) ? 0 : 1);
            LPC_PUSH(out);
            break;
        }

        case Op::Return: {
            const std::uint32_t base = fp->base;
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                last_result_ = Value::Nil();
            } else {
                last_result_ = LPC_POP();
            }

            std::string returning_module_name = std::move(fp->module_name);
            const std::uint64_t returning_version_id = fp->module_version_id;
            const bool returning_version_pinned = fp->version_pinned;
            LPC_SYNC_SP();
            frames_.pop_back();
            if (returning_version_pinned && returning_version_id != 0) {
                RuntimeError unpin_err = hot_reload_manager_.UnpinVersion(returning_module_name, returning_version_id);
                if (!unpin_err.ok()) {
                    if (catch_ctx.active) throw CatchSignal();
                    return unpin_err;
                }
            }

            if (value_stack_.size() > base) {
                value_stack_.resize(base);
            }
            LPC_LOAD_SP();
            if (!frames_.empty()) {
                LPC_PUSH(last_result_);
                fp = &frames_.back();
            }
            break;
        }

        case Op::LoadLocal: {
#ifndef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
#else
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
#endif
            LPC_PUSH(value_stack_[fp->base + idx]);
            break;
        }

        case Op::StoreLocal: {
#ifndef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(idx >= curf.nlocals)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreLocal requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
#else
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
#endif
            value_stack_[fp->base + idx] = LPC_POP();
            break;
        }

        case Op::LoadGlobal: {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            std::vector<Value> *globals = &BoundChunk().globals;
            if (fp->object_id > 0 && fp->object_id <= objects_.size()) {
                globals = &objects_[fp->object_id - 1].globals;
            }
            if (idx >= globals->size()) {
                LPC_PUSH(Value::Nil());
            } else {
                LPC_PUSH((*globals)[idx]);
            }
            break;
        }

        case Op::StoreGlobal: {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreGlobal requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::vector<Value> *globals = &BoundChunk().globals;
            if (fp->object_id > 0 && fp->object_id <= objects_.size()) {
                globals = &objects_[fp->object_id - 1].globals;
            }
            LPC_SYNC_SP();
            if (idx >= globals->size()) {
                globals->resize(static_cast<std::size_t>(idx) + 1, Value::Nil());
            }
            (*globals)[idx] = LPC_TOP();
            LPC_DISCARD();
            LPC_LOAD_SP();
            break;
        }

        case Op::LoadUpvalue: {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(fp->closure_slot >= closures_.size() ||
                idx >= closures_[fp->closure_slot].UpvalueCount())) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "LoadUpvalue out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(closures_[fp->closure_slot].GetUpvalue(idx));
            break;
        }

        case Op::StoreUpvalue: {
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreUpvalue requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (fp->closure_slot < closures_.size() &&
                idx < closures_[fp->closure_slot].UpvalueCount()) {
                closures_[fp->closure_slot].SetUpvalue(idx, LPC_TOP());
            }
            LPC_DISCARD();
            break;
        }

        case Op::Jump: {
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Jump operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
            if (next_ip < static_cast<std::int64_t>(curf.code_start) ||
                next_ip >= static_cast<std::int64_t>(curf.code_end)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "jump target out of function range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            fp->ip = static_cast<std::uint32_t>(next_ip);
            break;
        }

        case Op::JumpIfFalse: {
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated JumpIfFalse operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "JumpIfFalse requires condition";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value cond = LPC_POP();
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            if (!IsTruthy(cond)) {
                std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
                if (next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end)) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "jump target out of function range";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                fp->ip = static_cast<std::uint32_t>(next_ip);
            }
            break;
        }

        case Op::JumpIfTrue: {
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated JumpIfTrue operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "JumpIfTrue requires condition";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value cond = LPC_POP();
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
            if (IsTruthy(cond)) {
                std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
                if (next_ip < static_cast<std::int64_t>(curf.code_start) ||
                    next_ip >= static_cast<std::int64_t>(curf.code_end)) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "jump target out of function range";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                fp->ip = static_cast<std::uint32_t>(next_ip);
            }
            break;
        }

        case Op::CallDirect: {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallDirect operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            std::uint16_t fid = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            std::uint16_t argc = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::size_t stack_size = LPC_STACK_SIZE();
            const auto &functions = BoundChunk().functions;
            if (fid >= functions.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "call target out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const FunctionProto &callee = functions[fid];
            RuntimeError stack_err = ensure_call_stack_room();
            if (!stack_err.ok()) {
                return stack_err;
            }
            if (argc != callee.arity) {
                RuntimeError e;
                e.code = RuntimeErrorCode::ArityMismatch;
                e.message = "call argc mismatch";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (stack_size < static_cast<std::size_t>(fp->stack_top) + argc) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "call stack underflow";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }

            const std::uint32_t callee_base = static_cast<std::uint32_t>(stack_size - argc);
            extend_call_locals(callee, argc);

            RuntimeError call_err = push_frame(
                fid,
                callee.code_start,
                callee_base,
                callee_base + callee.nlocals,
                fp->object_id,
                fp->module_version_id,
                fp->module_name,
                true,
                0);
            if (!call_err.ok()) {
                return call_err;
            }
            fp = &frames_.back();
            break;
        }

        case Op::CallValue: {
            if (LPC_UNLIKELY(fp->ip + 2 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallValue operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t argc = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::size_t stack_size_before_pop = LPC_STACK_SIZE();
            if (stack_size_before_pop < static_cast<std::size_t>(fp->stack_top) + 1u + argc) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "CallValue stack underflow";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value callee_val = LPC_POP();
            const auto &functions = BoundChunk().functions;
            const std::size_t fn_count = functions.size();
            std::uint16_t fid = 0;
            std::uint64_t callee_version = fp->module_version_id;
            std::uint32_t closure_slot = 0;
            if (IsFuncObjRef(callee_val)) {
                std::size_t raw_fid = DecodeFuncId(callee_val);
                if (raw_fid == 0 || raw_fid > fn_count) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "CallValue: invalid function handle";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                fid = static_cast<std::uint16_t>(raw_fid - 1);
            } else if (callee_val.IsClosure()) {
                std::uint32_t cid = callee_val.ClosureId();
                if (cid == 0 || cid > closures_.size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "CallValue: invalid closure handle";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                const LpcClosure &cl = closures_[cid - 1];
                fid = cl.FuncId();
                if (fid >= fn_count) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "CallValue: closure target function out of range";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                callee_version = cl.ModuleVersionId();
                closure_slot = cid - 1;
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "CallValue: callee is not a function or closure";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const FunctionProto &callee = functions[fid];
            RuntimeError stack_err = ensure_call_stack_room();
            if (!stack_err.ok()) {
                return stack_err;
            }
            if (argc != callee.arity) {
                RuntimeError e;
                e.code = RuntimeErrorCode::ArityMismatch;
                e.message = "CallValue argc mismatch";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint32_t callee_base = static_cast<std::uint32_t>(LPC_STACK_SIZE() - argc);
            extend_call_locals(callee, argc);
            RuntimeError call_err = push_frame(
                fid,
                callee.code_start,
                callee_base,
                callee_base + callee.nlocals,
                fp->object_id,
                callee_version,
                fp->module_name,
                callee_version == fp->module_version_id,
                closure_slot);
            if (!call_err.ok()) {
                return call_err;
            }
            fp = &frames_.back();
            break;
        }

        case Op::CallIntrinsic: {
            if (LPC_UNLIKELY(fp->ip + 3 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallIntrinsic operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 3;
            std::uint16_t efun_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            std::uint8_t argc = p[2];

            if (efun_idx == static_cast<std::uint16_t>(Efun::CallOther)) {
                const auto fail_call_other_nil = [&](std::size_t new_size) {
                    value_stack_.resize(new_size);
                    LPC_LOAD_SP();
                    LPC_PUSH(Value::Nil());
                };
                const std::size_t stack_size = LPC_STACK_SIZE();
                if (argc < 2 || stack_size < argc) {
                    const std::size_t discard_n = std::min<std::size_t>(static_cast<std::size_t>(argc), value_stack_.size());
                    fail_call_other_nil(value_stack_.size() - discard_n);
                    break;
                }

                const std::size_t arg_base = stack_size - argc;
                const Value target_arg = value_stack_[arg_base];
                const Value func_arg = value_stack_[arg_base + 1];
                if (!func_arg.IsObjRef()) {
                    fail_call_other_nil(arg_base);
                    break;
                }
                const std::string func_name = ResolveObjRefStringOnly(func_arg);

                const std::string *target_module_name = &fp->module_name;
                std::uint64_t target_version_id = fp->module_version_id;
                const VersionRuntimeData *target_runtime = bound_vrdata_;
                std::string string_target_module;
                std::uint32_t target_object_id = 0;
                if (!target_runtime) {
                    fail_call_other_nil(arg_base);
                    break;
                }

                if (target_arg.IsObjRef() && IsStringObjRefFull(target_arg)) {
                    const std::string requested_module = ResolveObjRefStringOnly(target_arg);
                    ModuleRuntimeState *tms = nullptr;
                    RuntimeError module_err = EnsureModuleLoaded(requested_module, fp->module_name, &tms, &string_target_module);
                    if (!module_err.ok() || !tms || tms->active_version_id == 0) {
                        fail_call_other_nil(arg_base);
                        break;
                    }
                    auto vit = tms->version_runtime_data.find(tms->active_version_id);
                    if (vit == tms->version_runtime_data.end()) {
                        fail_call_other_nil(arg_base);
                        break;
                    }
                    target_module_name = &string_target_module;
                    target_version_id = tms->active_version_id;
                    target_runtime = &vit->second;
                } else {
                    const std::size_t target_oid = DecodeObjectId(target_arg);
                    const bool target_valid = target_oid > 0 && target_oid <= objects_.size();
                    if (!target_valid) {
                        fail_call_other_nil(arg_base);
                        break;
                    }
                    const LpcObject *target_obj = &objects_[target_oid - 1];
                    if (target_obj->destroyed) {
                        fail_call_other_nil(arg_base);
                        break;
                    }

                    TryUpgradeObject(target_oid);
                    target_obj = &objects_[target_oid - 1];
                    if (target_obj->destroyed) {
                        fail_call_other_nil(arg_base);
                        break;
                    }
                    target_object_id = static_cast<std::uint32_t>(target_oid);

                    if (!target_obj->module_name.empty() && target_obj->module_name != *target_module_name) {
                        ModuleRuntimeState *tms = GetModuleState(target_obj->module_name);
                        if (tms && tms->active_version_id != 0) {
                            auto vit = tms->version_runtime_data.find(tms->active_version_id);
                            if (vit != tms->version_runtime_data.end()) {
                                target_module_name = &target_obj->module_name;
                                target_version_id = tms->active_version_id;
                                target_runtime = &vit->second;
                            }
                        }
                    }
                }

                const int fid = target_runtime->FindFunction(func_name);
                if (fid < 0) {
                    fail_call_other_nil(arg_base);
                    break;
                }

                const FunctionProto &callee = target_runtime->chunk.functions[fid];
                const std::uint16_t call_argc = static_cast<std::uint16_t>(argc - 2u);
                for (std::uint16_t i = 0; i < call_argc; ++i) {
                    value_stack_[arg_base + i] = value_stack_[arg_base + 2 + i];
                }
                value_stack_.resize(arg_base + call_argc);
                LPC_LOAD_SP();

                RuntimeError stack_err = ensure_call_stack_room();
                if (!stack_err.ok()) {
                    return stack_err;
                }

                const std::uint32_t callee_base = static_cast<std::uint32_t>(value_stack_.size() - call_argc);
                extend_call_locals(callee, call_argc);

                RuntimeError call_err = push_frame(
                    static_cast<std::uint32_t>(fid),
                    callee.code_start,
                    callee_base,
                    callee_base + callee.nlocals,
                    target_object_id,
                    target_version_id,
                    *target_module_name,
                    target_version_id == fp->module_version_id && *target_module_name == fp->module_name,
                    0);
                if (!call_err.ok()) {
                    return call_err;
                }
                fp = &frames_.back();
                break;
            }

            LPC_SYNC_SP();
            Value result = DispatchIntrinsic(efun_idx, argc);

            Efun efun = static_cast<Efun>(efun_idx);
            if (!EfunIsVoid(efun)) {
                LPC_PUSH(result);
            }
            LPC_LOAD_SP();
            break;
        }

        case Op::CallVirtual: {
            const auto fail_call_virtual = [&](std::uint16_t discard_argc, const std::string &msg) -> RuntimeError {
                const std::size_t discard_n = std::min<std::size_t>(static_cast<std::size_t>(discard_argc), value_stack_.size());
                value_stack_.resize(value_stack_.size() - discard_n);
                LPC_LOAD_SP();
                RuntimeError e;
                e.code = RuntimeErrorCode::UndefinedFunction;
                e.message = msg;
                if (catch_ctx.active) throw CatchSignal();
                return e;
            };
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated CallVirtual operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            std::uint16_t name_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            std::uint16_t argc = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::size_t stack_size = LPC_STACK_SIZE();
            if (name_idx >= BoundChunk().sconst.size()) {
                return fail_call_virtual(argc, "CallVirtual: undefined function ''");
            }
            const std::string &func_name = BoundChunk().sconst[name_idx];
            int fid = FindBoundFunction(func_name);
            if (fid < 0) {
                return fail_call_virtual(argc, "CallVirtual: undefined function '" + func_name + "'");
            }
            RuntimeError stack_err = ensure_call_stack_room();
            if (!stack_err.ok()) {
                return stack_err;
            }
            const auto &functions = BoundChunk().functions;
            const FunctionProto &callee = functions[fid];
            const std::uint32_t callee_base = static_cast<std::uint32_t>(
                stack_size >= argc ? stack_size - argc : 0);
            extend_call_locals(callee, argc);
            RuntimeError call_err = push_frame(
                static_cast<std::uint32_t>(fid),
                callee.code_start,
                callee_base,
                callee_base + callee.nlocals,
                fp->object_id,
                fp->module_version_id,
                fp->module_name,
                true,
                0);
            if (!call_err.ok()) {
                return call_err;
            }
            fp = &frames_.back();
            break;
        }

        case Op::LoadFunc: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadFunc operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t idx = ReadU16(BoundChunk().code, &fp->ip);
            if (idx >= BoundChunk().functions.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "LoadFunc index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const FunctionProto &fproto = BoundChunk().functions[idx];
            if (!fproto.upvalues.empty()) {
                LpcClosure cl;
                cl.SetFuncId(static_cast<std::uint16_t>(idx));
                cl.SetModuleVersionId(fp->module_version_id);
                cl.InitUpvalues(fproto.upvalues.size(), Value::Nil());
                for (std::size_t ui = 0; ui < fproto.upvalues.size(); ++ui) {
                    const UpvalueProto &uv = fproto.upvalues[ui];
                    constexpr std::uint16_t upvalue_from_parent_local = 0;
                    if (uv.source_kind == upvalue_from_parent_local) {
                        if (fp->base + uv.source_index < LPC_STACK_SIZE()) {
                            cl.SetUpvalue(ui, value_stack_[fp->base + uv.source_index]);
                        }
                    } else {
                        if (fp->closure_slot < closures_.size() &&
                            uv.source_index < closures_[fp->closure_slot].UpvalueCount()) {
                            cl.SetUpvalue(ui, closures_[fp->closure_slot].GetUpvalue(uv.source_index));
                        }
                    }
                }
                LPC_PUSH(AllocateClosureHandle(std::move(cl)));
            } else {
                LPC_PUSH(MakeFuncHandle(static_cast<std::size_t>(idx) + 1));
            }
            break;
        }

        case Op::NewClass: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated NewClass operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t class_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (class_idx >= BoundChunk().classes.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(AllocateClassHandle(class_idx));
            break;
        }

        case Op::SetClassField: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated SetClassField operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t field_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "SetClassField requires value + class";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value clazz = LPC_POP();
            Value val = LPC_POP();
            if (!clazz.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "SetClassField expects class object";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::size_t cls_id = DecodeClassId(clazz);
            if (cls_id == 0 || cls_id > class_fields_.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class handle out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LpcClass &fields = class_fields_[cls_id - 1];
            if (field_idx >= fields.Size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class field index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            fields.Set(field_idx, val);
            break;
        }

        case Op::LoadClassField: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadClassField operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t field_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "LoadClassField requires class object";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value clazz = LPC_POP();
            if (!clazz.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "LoadClassField expects class object";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::size_t cls_id = DecodeClassId(clazz);
            if (cls_id == 0 || cls_id > class_fields_.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class handle out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LpcClass &fields = class_fields_[cls_id - 1];
            if (field_idx >= fields.Size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class field index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_PUSH(fields.At(field_idx));
            break;
        }

        case Op::NewArray: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated NewArray operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t n = ReadU16(BoundChunk().code, &fp->ip);
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + n) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "NewArray stack underflow";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LpcArray arr(n, Value::Nil());
            for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
                arr.Set(static_cast<std::size_t>(i), LPC_POP());
            }
            LPC_SYNC_SP();
            LPC_PUSH(AllocateArrayHandle(std::move(arr)));
            LPC_LOAD_SP();
            break;
        }

        case Op::NewMapping: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated NewMapping operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t npairs = ReadU16(BoundChunk().code, &fp->ip);
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + static_cast<std::size_t>(npairs) * 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "NewMapping stack underflow";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Mapping map;
            for (int i = static_cast<int>(npairs) - 1; i >= 0; --i) {
                Value val = LPC_POP();
                Value key = LPC_POP();
                map.Insert(key, val);
            }
            LPC_SYNC_SP();
            LPC_PUSH(AllocateMappingHandle(std::move(map)));
            LPC_LOAD_SP();
            break;
        }

        case Op::Index: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Index requires container + key";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value key = LPC_POP();
            Value arrv = LPC_POP();
            if (arrv.IsObjRef() && key.Tag() == ValueTag::Int64) {
                const std::int64_t key_i = GetI64(key);
                if (IsStringObjRef(arrv)) {
                    LPC_SYNC_SP();
                    std::string s = ResolveObjRefStringOnly(arrv);
                    if (key_i < 0 || static_cast<std::size_t>(key_i) >= s.size()) {
                        RuntimeError e;
                        e.code = RuntimeErrorCode::BoundsError;
                        e.message = "string index out of range";
                        if (catch_ctx.active) throw CatchSignal();
                        return e;
                    } else {
                        std::string ch(1, s[static_cast<std::size_t>(key_i)]);
                        LPC_PUSH(InternString(ch));
                    }
                    LPC_LOAD_SP();
                    break;
                }
                std::size_t arr_id = DecodeArrayId(arrv);
                if (arr_id == 0 || arr_id > arrays_.size()) {
                    std::size_t map_id = DecodeMappingId(arrv);
                    if (map_id > 0 && map_id <= mappings_.size()) {
                        const Value *found = mappings_[map_id - 1].Find(key);
                        LPC_PUSH(found ? *found : Value::Nil());
                        break;
                    }
                    RuntimeError e;
                    e.code = RuntimeErrorCode::InvalidOperand;
                    e.message = "array handle out of range";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                LpcArray &arr = arrays_[arr_id - 1];
                if (key_i < 0 || static_cast<std::size_t>(key_i) >= arr.Size()) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::BoundsError;
                    e.message = "array index out of range";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                LPC_PUSH(arr.At(static_cast<std::size_t>(key_i)));
            } else if (arrv.IsObjRef()) {
                std::size_t map_id = DecodeMappingId(arrv);
                if (map_id > 0 && map_id <= mappings_.size()) {
                    const Value *found = mappings_[map_id - 1].Find(key);
                    LPC_PUSH(found ? *found : Value::Nil());
                } else {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::TypeError;
                    e.message = "Index: container is not array, string, or mapping";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
            } else if (arrv.IsNil()) {
                LPC_PUSH(Value::Nil());
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Index expects array/string/mapping + key";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::StoreIndex: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 3) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "StoreIndex requires val + container + key";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value key = LPC_POP();
            Value arrv = LPC_POP();
            Value val = LPC_POP();
            if (!arrv.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "StoreIndex expects object container";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            if (key.Tag() == ValueTag::Int64) {
                std::size_t arr_id = DecodeArrayId(arrv);
                if (arr_id > 0 && arr_id <= arrays_.size()) {
                    LpcArray &arr = arrays_[arr_id - 1];
                    if (GetI64(key) < 0 || static_cast<std::size_t>(GetI64(key)) >= arr.Size()) {
                        RuntimeError e;
                        e.code = RuntimeErrorCode::BoundsError;
                        e.message = "array index out of range";
                        if (catch_ctx.active) throw CatchSignal();
                        return e;
                    }
                    arr.Set(static_cast<std::size_t>(GetI64(key)), val);
                    LPC_PUSH(val);
                    break;
                }
            }
            std::size_t map_id = DecodeMappingId(arrv);
            if (map_id > 0 && map_id <= mappings_.size()) {
                mappings_[map_id - 1].Insert(key, val);
                LPC_PUSH(val);
                break;
            }
            RuntimeError e;
            e.code = RuntimeErrorCode::TypeError;
            e.message = "StoreIndex: container is not array or mapping";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }

        case Op::Upset: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Upset sub-ops";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint8_t sub_op = BoundChunk().code[fp->ip++];
            bool is_prefix = (BoundChunk().code[fp->ip++] != 0);
            bool is_inc = (sub_op == kUpsetSubOpInc);
            bool is_dec = (sub_op == kUpsetSubOpDec);
            bool is_mutate = is_inc || is_dec;

            if (is_mutate) {
                if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "Upset inc/dec requires container + key";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value key = LPC_POP();
                Value container = LPC_POP();
                Value old_val = Value::Nil();
                Value new_val = Value::Nil();

                bool handled = false;
                if (container.IsObjRef() && key.Tag() == ValueTag::Int64) {
                    std::size_t arr_id = DecodeArrayId(container);
                    if (arr_id > 0 && arr_id <= arrays_.size()) {
                        auto &arr = arrays_[arr_id - 1];
                        std::size_t idx = static_cast<std::size_t>(GetI64(key));
                        if (idx < arr.Size()) {
                            if (arr.At(idx).Tag() == ValueTag::Int64) {
                                old_val = arr.At(idx);
                                arr.At(idx) = MakeI64(GetI64(arr.At(idx)) + (is_inc ? 1 : -1));
                                new_val = arr.At(idx);
                                handled = true;
                            }
                        }
                    }
                }
                if (!handled && container.IsObjRef()) {
                    std::size_t map_id = DecodeMappingId(container);
                    if (map_id > 0 && map_id <= mappings_.size()) {
                        auto &map = mappings_[map_id - 1];
                        Value *slot = map.Find(key);
                        if (slot && slot->Tag() == ValueTag::Int64) {
                            old_val = *slot;
                            *slot = MakeI64(GetI64(*slot) + (is_inc ? 1 : -1));
                            new_val = *slot;
                        }
                    }
                }
                LPC_PUSH(is_prefix ? new_val : old_val);
            } else {
                if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 3) {
                    RuntimeError e;
                    e.code = RuntimeErrorCode::StackUnderflow;
                    e.message = "Upset requires container + key + value";
                    if (catch_ctx.active) throw CatchSignal();
                    return e;
                }
                Value val = LPC_POP();
                Value key = LPC_POP();
                Value container = LPC_POP();
                Value result = val;

                if (container.IsObjRef() && key.Tag() == ValueTag::Int64) {
                    std::size_t arr_id = DecodeArrayId(container);
                    if (arr_id > 0 && arr_id <= arrays_.size()) {
                        auto &arr = arrays_[arr_id - 1];
                        std::size_t idx = static_cast<std::size_t>(GetI64(key));
                        if (idx < arr.Size()) {
                            arr.Set(idx, val);
                            result = val;
                        }
                    }
                }
                if (container.IsObjRef()) {
                    std::size_t arr_id2 = DecodeArrayId(container);
                    if (arr_id2 == 0 || arr_id2 > arrays_.size()) {
                        std::size_t map_id = DecodeMappingId(container);
                        if (map_id > 0 && map_id <= mappings_.size()) {
                            mappings_[map_id - 1].Insert(key, val);
                            result = val;
                        }
                    }
                }
                LPC_PUSH(result);
            }
            break;
        }

        case Op::SubArr: {
            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 3) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "SubArr requires container + start + end";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value end_v = LPC_POP();
            Value start_v = LPC_POP();
            Value arrv = LPC_POP();
            if (!arrv.IsObjRef() || start_v.Tag() != ValueTag::Int64 || end_v.Tag() != ValueTag::Int64) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "SubArr expects array + Int64 start + Int64 end";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::size_t arr_id = DecodeArrayId(arrv);
            if (arr_id == 0 || arr_id > arrays_.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "array handle out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const LpcArray &src = arrays_[arr_id - 1];
            std::int64_t s = GetI64(start_v);
            std::int64_t e2 = GetI64(end_v);
            if (s < 0) s = 0;
            if (e2 > static_cast<std::int64_t>(src.Size())) e2 = static_cast<std::int64_t>(src.Size());
            std::vector<Value> sub;
            for (std::int64_t i = s; i < e2; ++i) {
                sub.push_back(src.At(static_cast<std::size_t>(i)));
            }
            LPC_PUSH(AllocateArrayHandle(std::move(sub)));
            break;
        }

        case Op::Pop: {
#ifndef NDEBUG
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Pop requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
#endif
            LPC_DISCARD();
            break;
        }

        case Op::Dup: {
#ifndef NDEBUG
            if (LPC_UNLIKELY(LPC_STACK_SIZE() <= fp->stack_top)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Dup requires one operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
#endif
            LPC_PUSH(LPC_TOP());
            break;
        }

        case Op::ForeachStep1: {
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "ForeachStep1 requires container";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value iter;
            iter = Value::FromI64(0);
            LPC_PUSH(iter);
            break;
        }

        case Op::ForeachStep2: {
            if (fp->ip + 7 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated ForeachStep2 operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint8_t sz = BoundChunk().code[fp->ip++];
            std::uint16_t slot0 = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t slot1 = ReadU16(BoundChunk().code, &fp->ip);
            std::int16_t rel = ReadI16(BoundChunk().code, &fp->ip);

            if (LPC_STACK_SIZE() < static_cast<std::size_t>(fp->stack_top) + 2) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "ForeachStep2 requires container + iter";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value iter = LPC_POP();
            Value container = LPC_TOP();

            bool done = false;
            if (!container.IsObjRef()) {
                done = true;
            } else {
                LPC_SYNC_SP();
                std::size_t arr_id = DecodeArrayId(container);
                if (arr_id == 0 || arr_id > arrays_.size()) {
                    done = true;
                } else {
                    LpcArray &arr = arrays_[arr_id - 1];
                    std::int64_t index = (iter.Tag() == ValueTag::Int64) ? GetI64(iter) : 0;
                    if (index >= static_cast<std::int64_t>(arr.Size())) {
                        done = true;
                    } else {
                        iter = Value::FromI64(index + 1);
                        LPC_PUSH(iter);
                        if (sz == 1) {
                            std::uint32_t dest = fp->base + slot0;
                            if (dest < LPC_STACK_SIZE()) {
                                value_stack_[dest] = arr.At(static_cast<std::size_t>(index));
                            }
                        } else if (sz == 2) {
                            std::uint32_t d0 = fp->base + slot0;
                            std::uint32_t d1 = fp->base + slot1;
                            if (d0 < LPC_STACK_SIZE()) {
                                value_stack_[d0] = Value::Nil();
                            }
                            if (d1 < LPC_STACK_SIZE()) {
                                value_stack_[d1] = arr.At(static_cast<std::size_t>(index));
                            }
                        }
                    }
                }
            }

            if (done) {
                LPC_DISCARD();
                fp->ip = static_cast<std::uint32_t>(static_cast<std::int32_t>(fp->ip) + rel);
            }
            break;
        }

        case Op::Catch: {
            if (fp->ip + 2 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Catch operand";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::int16_t rel = ReadI16(BoundChunk().code, &fp->ip);
            catch_ctx.active = true;
            catch_ctx.end_ip = static_cast<std::uint32_t>(static_cast<std::int32_t>(fp->ip) + rel);
            catch_ctx.stack_watermark = LPC_STACK_SIZE();
            break;
        }

        case Op::Switch: {
            if (fp->ip + 6 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated Switch operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            fp->ip += 6;
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                RuntimeError e;
                e.code = RuntimeErrorCode::StackUnderflow;
                e.message = "Switch requires discriminant";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            LPC_DISCARD();
            RuntimeError e;
            e.code = RuntimeErrorCode::InternalError;
            e.message = "Switch not yet implemented";
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }

        default: {
            RuntimeError e;
            e.code = RuntimeErrorCode::InvalidOpcode;
            e.message = "unknown NextVM opcode";
            e.function = curf.name;
            e.pc = static_cast<int>(fp->ip - 1);
            if (catch_ctx.active) throw CatchSignal();
            return e;
        }

        }

        }

#if LPC_ENABLE_COMPUTED_GOTO
lpc_post_dispatch:
#endif
        if (catch_ctx.active && fp->ip >= catch_ctx.end_ip) {
            if (LPC_STACK_SIZE() > catch_ctx.stack_watermark) {
                LPC_SYNC_SP();
                value_stack_.resize(catch_ctx.stack_watermark);
                LPC_LOAD_SP();
            }
            LPC_PUSH(Value::FromI64(0));
            catch_ctx.active = false;
            catch_ctx.end_ip = 0;
        }
        } catch (const CatchSignal &) {
            if (catch_ctx.active) {
                if (debugger_.active() && debugger_.break_on_exceptions()) {
                    debugger_.set_last_break_exception(true);
                    debugger_.SetStepMode(StepMode::Continue, static_cast<std::uint32_t>(frames_.size()));
                    if (debug_hook_) {
                        RuntimeError hook_err = debug_hook_(fp->ip);
                        if (!hook_err.ok()) return hook_err;
                    }
                }
                LPC_SYNC_SP();
                fp = &frames_.back();
                fp->ip = catch_ctx.end_ip;
                if (value_stack_.size() > catch_ctx.stack_watermark) {
                    value_stack_.resize(catch_ctx.stack_watermark);
                }
                LPC_LOAD_SP();
                LPC_PUSH(Value::FromI64(1));
                catch_ctx.active = false;
                catch_ctx.end_ip = 0;
                continue;
            }
            throw;
        }
    }

#undef LPC_PUSH
#undef LPC_POP
#undef LPC_TOP
#undef LPC_PEEK
#undef LPC_STACK_SZ
#undef LPC_STACK_SIZE
#undef LPC_SYNC_SP
#undef LPC_LOAD_SP
#undef LPC_DISCARD

    EndProfile();
    RuntimeError rebind_err = RebindActiveChunkForCurrentModule();
    if (!rebind_err.ok()) {
        return rebind_err;
    }
    return {};
}
