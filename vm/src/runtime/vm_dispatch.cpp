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
#include <limits>
#include <thread>

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

    entry_lifecycle_queue_.clear();
    if (BoundChunk().create_idx != kInvalidIndex16) {
        PendingLifecycleCall plc;
        plc.func_id = BoundChunk().create_idx;
        plc.object_id = 1;
        plc.module_version_id = current_module_version_id_;
        plc.module_name = current_module_name_;
        entry_lifecycle_queue_.push_back(std::move(plc));
    }
    if (BoundChunk().on_loadin_idx != kInvalidIndex16) {
        PendingLifecycleCall plc;
        plc.func_id = BoundChunk().on_loadin_idx;
        plc.object_id = 1;
        plc.module_version_id = current_module_version_id_;
        plc.module_name = current_module_name_;
        entry_lifecycle_queue_.push_back(std::move(plc));
    }
    {
        PendingLifecycleCall plc;
        plc.func_id = static_cast<std::uint16_t>(func_id);
        plc.object_id = 1;
        plc.module_version_id = current_module_version_id_;
        plc.module_name = current_module_name_;
        plc.is_entry = true;
        entry_lifecycle_queue_.push_back(std::move(plc));
    }

    frames_.clear();
    frames_.reserve(kMaxCallFrames);
    EnsureStackCapacity();
    StackClear();

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

#ifdef NDEBUG
#define LPC_PUSH(v)    do { *sp++ = (v); } while(0)
#define LPC_POP()      ({ Value _v = *--sp; _v; })
#define LPC_DISCARD()  do { --sp; } while(0)
#define LPC_COMMIT_SP() do { value_stack_sp_ = sp; } while(0)
#define LPC_SYNC_SP()  do { sp = value_stack_sp_; } while(0)
#define LPC_LOAD_SP()  do { sp = value_stack_sp_; } while(0)
#else
#define LPC_PUSH(v)    do { *sp++ = (v); } while(0)
#define LPC_POP()      ({ Value _v = *--sp; _v; })
#define LPC_DISCARD()  do { --sp; } while(0)
#define LPC_COMMIT_SP() do { value_stack_sp_ = sp; } while(0)
#define LPC_SYNC_SP()  do { sp = value_stack_sp_; } while(0)
#define LPC_LOAD_SP()  do { sp = value_stack_sp_; } while(0)
#endif
#define LPC_TOP()      sp[-1]
#define LPC_PEEK(n)    sp[-1-(n)]
#define LPC_STACK_SZ() static_cast<std::size_t>(sp - value_stack_)
#define LPC_STACK_SIZE() LPC_STACK_SZ()

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

    Value *sp = value_stack_sp_;
    bool need_rebind = false;
    std::size_t inner_dispatch_count = 0;

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
            LPC_COMMIT_SP();
            StackResize(StackSize() + local_slots);
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

    auto schedule_due_timer = [&]() -> RuntimeError {
        const auto now = std::chrono::steady_clock::now();
        if (timer_pending_count_ == 0 || timer_next_due_ > now) {
            return RuntimeError::Ok();
        }

        auto maybe_rebuild_timer_heap = [&]() {
            if (timer_heap_.size() < kTimerHeapRebuildMinSize) return;
            if (timer_heap_stale_pops_ * kTimerHeapStaleRatio < timer_heap_.size()) return;
            std::priority_queue<TimerHeapEntry, std::vector<TimerHeapEntry>, TimerHeapCmp> rebuilt;
            for (const auto &timer : timers_) {
                if (timer.cancelled) continue;
                rebuilt.push(TimerHeapEntry{timer.due_at, timer.id});
            }
            timer_heap_ = std::move(rebuilt);
            timer_heap_stale_pops_ = 0;
            ++timer_heap_rebuild_count_;
            if (!timer_heap_.empty()) {
                timer_next_due_ = timer_heap_.top().due_at;
            } else {
                timer_next_due_ = std::chrono::steady_clock::time_point::max();
            }
        };

        while (!timer_heap_.empty()) {
            const TimerHeapEntry top = timer_heap_.top();
            auto idx_it = timer_id_index_.find(top.id);
            if (idx_it == timer_id_index_.end()) {
                timer_heap_.pop();
                ++timer_heap_stale_pops_;
                maybe_rebuild_timer_heap();
                continue;
            }
            const std::size_t idx = idx_it->second;
            if (idx >= timers_.size()) {
                timer_id_index_.erase(idx_it);
                timer_heap_.pop();
                ++timer_heap_stale_pops_;
                maybe_rebuild_timer_heap();
                continue;
            }
            TimerTask &ref = timers_[idx];
            if (ref.cancelled || ref.id != top.id) {
                timer_id_index_.erase(idx_it);
                timer_heap_.pop();
                ++timer_heap_stale_pops_;
                maybe_rebuild_timer_heap();
                continue;
            }
            if (top.due_at > now) {
                timer_next_due_ = top.due_at;
                return RuntimeError::Ok();
            }

            TimerTask task = std::move(ref);
            ref.cancelled = true;
            timer_id_index_.erase(idx_it);
            timer_heap_.pop();

            ++timer_fired_count_;
            if (timer_pending_count_ > 0) {
                --timer_pending_count_;
            }
            auto mit = module_timer_pending_count_.find(task.module_name);
            if (mit != module_timer_pending_count_.end()) {
                if (mit->second > 1) {
                    --mit->second;
                } else {
                    module_timer_pending_count_.erase(mit);
                }
            }

            std::uint16_t fid = 0;
            std::uint32_t closure_slot = 0;
            std::uint64_t callee_version = task.module_version_id;
            if (IsFuncObjRef(task.callee)) {
                std::size_t raw_fid = DecodeFuncId(task.callee);
                if (raw_fid == 0) {
                    return RuntimeError::Ok();
                }
                fid = static_cast<std::uint16_t>(raw_fid - 1);
            } else if (task.callee.IsClosure()) {
                const std::uint32_t cid = task.callee.ClosureId();
                if (cid == 0 || cid > closures_.size()) {
                    return RuntimeError::Ok();
                }
                const LpcClosure &cl = closures_[cid - 1];
                fid = cl.FuncId();
                closure_slot = cid - 1;
                if (cl.ModuleVersionId() != 0) {
                    callee_version = cl.ModuleVersionId();
                }
            } else if (IsStringObjRefFull(task.callee)) {
                fid = kInvalidIndex16;
            } else {
                return RuntimeError::Ok();
            }

            if (task.module_name.empty()) {
                task.module_name = current_module_name_;
            }
            if (task.module_name.empty()) {
                return RuntimeError::Ok();
            }

            if (callee_version == 0) {
                const ModuleRuntimeState *state = GetModuleState(task.module_name);
                if (!state || state->active_version_id == 0) {
                    return RuntimeError::Ok();
                }
                callee_version = state->active_version_id;
            }

            RuntimeError bind_err = BindExecutionVersion(task.module_name, callee_version);
            if (!bind_err.ok()) {
                return bind_err;
            }

            const auto &functions = BoundChunk().functions;
            if (fid == kInvalidIndex16) {
                const std::string func_name = ResolveObjRefStringOnly(task.callee);
                const int named_fid = FindBoundFunction(func_name);
                if (named_fid < 0) {
                    return RuntimeError::Ok();
                }
                fid = static_cast<std::uint16_t>(named_fid);
            }
            if (fid >= functions.size()) {
                return RuntimeError::Ok();
            }
            const FunctionProto &callee = functions[fid];
            const std::uint16_t argc = static_cast<std::uint16_t>(task.args.size());
            if (argc != callee.arity) {
                return RuntimeError::Ok();
            }

            RuntimeError stack_err = ensure_call_stack_room();
            if (!stack_err.ok()) {
                return stack_err;
            }

            LPC_COMMIT_SP();
            for (const auto &arg : task.args) {
                StackPush(arg);
            }
            LPC_LOAD_SP();

            const std::uint32_t callee_base = static_cast<std::uint32_t>(LPC_STACK_SIZE() - argc);
            extend_call_locals(callee, argc);

            std::uint32_t target_object_id = task.object_id;
            if (target_object_id == 0 || target_object_id > objects_.size() ||
                objects_[target_object_id - 1].destroyed) {
                target_object_id = 0;
            }

            RuntimeError call_err = push_frame(
                fid,
                callee.code_start,
                callee_base,
                callee_base + callee.nlocals,
                target_object_id,
                callee_version,
                task.module_name,
                false,
                closure_slot);
            if (!call_err.ok()) {
                return call_err;
            }

            if (!timer_heap_.empty()) {
                timer_next_due_ = timer_heap_.top().due_at;
            } else {
                timer_next_due_ = std::chrono::steady_clock::time_point::max();
            }
            return RuntimeError::Ok();
        }

        timer_next_due_ = std::chrono::steady_clock::time_point::max();
        return RuntimeError::Ok();
    };

    auto compact_cancelled_timers = [&]() {
        if (timers_.empty()) return;
        std::size_t write = 0;
        for (std::size_t i = 0; i < timers_.size(); ++i) {
            if (timers_[i].cancelled) continue;
            if (write != i) {
                timers_[write] = std::move(timers_[i]);
            }
            ++write;
        }
        if (write < timers_.size()) {
            timers_.resize(write);
        }
    };

    while (true) {
        if (timer_pending_count_ > 0) {
            RuntimeError timer_err = schedule_due_timer();
            if (!timer_err.ok()) {
                return timer_err;
            }
        }
        if (frames_.empty()) {
            if (!entry_lifecycle_queue_.empty()) {
                PendingLifecycleCall plc = std::move(entry_lifecycle_queue_.front());
                entry_lifecycle_queue_.erase(entry_lifecycle_queue_.begin());
                const VersionRuntimeData *plc_vrd = bound_vrdata_;
                if (plc_vrd && plc.func_id < plc_vrd->chunk.functions.size()) {
                    const FunctionProto &callee = plc_vrd->chunk.functions[plc.func_id];
                    if (plc.is_entry || callee.arity == 0) {
                        StackClear();
                        LPC_LOAD_SP();
                        const std::uint32_t callee_base = 0;
                        StackResize(callee.nlocals);
                        LPC_LOAD_SP();
                        Frame lf;
                        lf.func_id = plc.func_id;
                        lf.ip = callee.code_start;
                        lf.base = callee_base;
                        lf.stack_top = callee_base + callee.nlocals;
                        lf.object_id = plc.object_id;
                        lf.module_version_id = plc.module_version_id;
                        lf.module_name = plc.module_name;
                        lf.version_pinned = false;
                        if (lf.module_version_id != 0) {
                            RuntimeError pin_err = hot_reload_manager_.PinVersion(lf.module_name, lf.module_version_id);
                            if (!pin_err.ok()) return pin_err;
                            lf.version_pinned = true;
                        }
                        frames_.push_back(std::move(lf));
#ifdef NDEBUG
                        need_rebind = true;
#endif
                        continue;
                    }
                }
            }
            compact_cancelled_timers();
            if (timer_pending_count_ == 0) {
                timer_next_due_ = std::chrono::steady_clock::time_point::max();
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (timer_next_due_ == std::chrono::steady_clock::time_point::max() && !timer_heap_.empty()) {
                timer_next_due_ = timer_heap_.top().due_at;
            }
            if (timer_next_due_ > now) {
                const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timer_next_due_ - now);
                std::this_thread::sleep_for(wait_ms > std::chrono::milliseconds(1)
                    ? std::chrono::milliseconds(1)
                    : wait_ms);
            }
            continue;
        }
        if (LPC_UNLIKELY(alloc_count_ >= gc_threshold_)) {
            LPC_COMMIT_SP();
            CollectGarbage();
            gc_threshold_ = std::max(gc_threshold_,
                (arrays_.size() + mappings_.size() + class_fields_.size() +
                 string_heap_.size() + closures_.size() + objects_.size()) * 2 + kGcThresholdPadding);
        }
        Frame *fp = &frames_.back();
        inner_dispatch_count = 0;
        if (LPC_UNLIKELY(bound_module_version_id_ != fp->module_version_id)) {
            LPC_COMMIT_SP();
            RuntimeError bind_err = BindExecutionVersion(fp->module_name, fp->module_version_id);
            if (!bind_err.ok()) {
                if (catch_ctx.active) throw CatchSignal();
                return bind_err;
            }
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
lpc_inner_dispatch:
        need_rebind = false;
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

        if (LPC_UNLIKELY(instruction_debug_checks && debugger_.active())) {
            LPC_COMMIT_SP();
            if (debugger_.ShouldBreak(fp->ip, static_cast<std::uint32_t>(frames_.size()), BoundChunk(), frames_, value_stack_, StackSize())) {
                debugger_.ClearStepOnBreak(static_cast<std::uint32_t>(frames_.size()));
                if (debug_hook_) {
                    RuntimeError hook_err = debug_hook_(fp->ip);
                    if (!hook_err.ok()) {
                        return hook_err;
                    }
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
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::Add)] = &&lpc_cg_add;
            kComputedGotoDispatch[static_cast<std::uint8_t>(Op::Return)] = &&lpc_cg_return;
            kComputedGotoDispatchInit = true;
        }
        const void *cg_target = kComputedGotoDispatch[static_cast<std::uint8_t>(op)];
        if (cg_target != nullptr) {
            goto *cg_target;
        }
        goto lpc_cg_fallback;
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
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

            const bool returning_version_pinned = fp->version_pinned;
            std::uint64_t returning_version_id = 0;
            std::string returning_module_name;
            if (returning_version_pinned) {
                returning_version_id = fp->module_version_id;
                returning_module_name = std::move(fp->module_name);
            }
            LPC_COMMIT_SP();
            frames_.pop_back();
            if (returning_version_pinned && returning_version_id != 0) {
                RuntimeError unpin_err = hot_reload_manager_.UnpinVersion(returning_module_name, returning_version_id);
                if (!unpin_err.ok()) {
                    if (catch_ctx.active) throw CatchSignal();
                    return unpin_err;
                }
            }

            LPC_COMMIT_SP();
            if (StackSize() > base) {
                StackResize(base);
            }
            LPC_LOAD_SP();

            if (!frames_.empty()) {
                LPC_PUSH(last_result_);
                fp = &frames_.back();
            }
            need_rebind = true;
            fast_handled = true;
            goto lpc_post_dispatch;
        }
#endif
        lpc_cg_fallback:
        if (LPC_LIKELY(op == Op::JumpIfLocalBitAndIConstEqIConstFalse)) {
            if (LPC_UNLIKELY(fp->ip + 8 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated local bitand compare jump operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 8;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t mask_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t expected_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            const std::int16_t rel = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(p[6]) | (static_cast<std::uint16_t>(p[7]) << 8));
#ifndef NDEBUG
            if (LPC_UNLIKELY(local_idx >= curf.nlocals || mask_idx >= iconst_size || expected_idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local bitand compare jump operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value mask = iconst_values[mask_idx];
            const Value expected = iconst_values[expected_idx];
            bool eq = false;
            if (LPC_LIKELY(lhs.IsInlineInt64() && mask.IsInlineInt64() && expected.IsInlineInt64())) {
                eq = (lhs.AsI64() & mask.AsI64()) == expected.AsI64();
            } else if (LPC_LIKELY(lhs.IsInt64() && mask.IsInt64() && expected.IsInt64())) {
                eq = (GetI64(lhs) & GetI64(mask)) == GetI64(expected);
            } else if (LPC_UNLIKELY(lhs.IsNil() || mask.IsNil())) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = mask.IsNil() ? 0 : GetI64(mask);
                eq = expected.IsInt64() && ((a & b) == GetI64(expected));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "BitAnd requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (!eq) {
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
            goto lpc_post_dispatch;
        }

        if (LPC_LIKELY(op == Op::BitAndLocalIConstToLocal || op == Op::BitOrLocalIConstToLocal || op == Op::BitXorLocalIConstToLocal)) {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated bitwise local operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "bitwise local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = iconst_values[rhs_idx];
            Value result;
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                const std::uint64_t a_p = lhs.bits_ & Value::kPayloadMask;
                const std::uint64_t b_p = rhs.bits_ & Value::kPayloadMask;
                std::uint64_t r_p;
                if (op == Op::BitAndLocalIConstToLocal) r_p = a_p & b_p;
                else if (op == Op::BitOrLocalIConstToLocal) r_p = a_p | b_p;
                else r_p = a_p ^ b_p;
                result.bits_ = Value::kInt64TagBits | r_p;
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const std::int64_t b = rhs.IsInt64() ? GetI64(rhs) : 0;
                std::int64_t r = 0;
                if (op == Op::BitAndLocalIConstToLocal) r = 0 & b;
                else if (op == Op::BitOrLocalIConstToLocal) r = 0 | b;
                else r = 0 ^ b;
                result = MakeI64(r);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "bitwise requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            fast_handled = true;
            goto lpc_post_dispatch;
        }

        if (LPC_LIKELY(op == Op::JumpIfLocalLtFalse || op == Op::JumpIfLocalIConstLteFalse)) {
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
            if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
                cond = op == Op::JumpIfLocalLtFalse
                    ? GetI64(lhs) < GetI64(rhs)
                    : GetI64(lhs) <= GetI64(rhs);
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                if (LPC_UNLIKELY(!(lhs.IsFloat64() || lhs.IsInt64()) ||
                                 !(rhs.IsFloat64() || rhs.IsInt64()))) {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                const double a = lhs.IsFloat64() ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                cond = op == Op::JumpIfLocalLtFalse ? a < b : a <= b;
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                cond = false;
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string a = ResolveString(lhs);
                std::string b = ResolveString(rhs);
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
            goto lpc_post_dispatch;
        }

        if (LPC_LIKELY(op == Op::Eq)) {
            if (LPC_LIKELY(LPC_STACK_SZ() >= fp->stack_top + 2) && LPC_LIKELY(fp->ip + 3 <= code_size)) {
                const Op next = static_cast<Op>(code[fp->ip]);
                if (LPC_LIKELY(next == Op::JumpIfFalse || next == Op::JumpIfTrue)) {
                    const Value rhs = LPC_TOP();
                    Value &lhs = LPC_PEEK(1);
                    if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                        const bool eq = lhs.AsI64() == rhs.AsI64();
                        const std::uint8_t *jp = code + fp->ip + 1;
                        const std::int16_t rel = static_cast<std::int16_t>(
                            static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                        const bool should_jump = (next == Op::JumpIfFalse) ? !eq : eq;
                        LPC_DISCARD();
                        LPC_DISCARD();
                        fp->ip += 3;
                        if (should_jump) {
                            fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                        }
                        fast_handled = true;
                        goto lpc_post_dispatch;
                    }
                }
            }
        }

        if (LPC_LIKELY(op == Op::LoadLocal)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            const std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            LPC_PUSH(value_stack_[fp->base + idx]);
            fast_handled = true;
#endif
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                LPC_PUSH(MakeI64(op == Op::LoadLocalAddIConst ? lhs.AsI64() + rhs.AsI64() : lhs.AsI64() - rhs.AsI64()));
            } else if (op == Op::LoadLocalAddIConst && LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string result;
                result.reserve(sv_a.size() + sv_b.size());
                result.append(sv_a);
                result.append(sv_b);
                LPC_PUSH(InternString(result));
                LPC_COMMIT_SP();
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
        } else if (LPC_LIKELY(op == Op::LoadLocalBitAndIConst)) {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated local expression operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t const_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(local_idx >= curf.nlocals || const_idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "local expression operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value rhs = iconst_values[const_idx];
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                LPC_PUSH(MakeI64(lhs.AsI64() & rhs.AsI64()));
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                LPC_PUSH(MakeI64(a & b));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "BitAnd requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::LoadLocalAddFConst || op == Op::LoadLocalSubFConst ||
                                  op == Op::LoadLocalMulFConst || op == Op::LoadLocalDivFConst)) {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated float local expr operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t fconst_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(local_idx >= curf.nlocals || fconst_idx >= fconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "float local expr operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value rhs = fconst_values[fconst_idx];
            if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_LIKELY(lhs.IsFloat64())) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsInt64())) {
                const double a = static_cast<double>(GetI64(lhs));
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const double a = 0.0;
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else r = a / b;
                LPC_PUSH(Value::FromF64(r));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "float arithmetic unsupported type";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::LoadLocalDupAddFConst || op == Op::LoadLocalDupSubFConst ||
                                   op == Op::LoadLocalDupMulFConst || op == Op::LoadLocalDupDivFConst)) {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated float local-dup expr operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t fconst_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(local_idx >= curf.nlocals || fconst_idx >= fconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "float local-dup expr operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value rhs = fconst_values[fconst_idx];
            if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_LIKELY(lhs.IsFloat64())) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsInt64())) {
                const double a = static_cast<double>(GetI64(lhs));
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const double a = 0.0;
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else r = a / b;
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "float arithmetic unsupported type";
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
        } else if (LPC_LIKELY(op == Op::CallDirect)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t fid = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t argc = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const auto &functions = BoundChunk().functions;
            const FunctionProto &callee = functions[fid];
            if (LPC_UNLIKELY(frames_.size() >= kMaxCallFrames)) {
                RuntimeError e; e.code = RuntimeErrorCode::InternalError; e.message = "call stack limit exceeded";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            if (LPC_UNLIKELY(argc != callee.arity)) {
                RuntimeError e; e.code = RuntimeErrorCode::ArityMismatch; e.message = "call argc mismatch";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::size_t stack_size = LPC_STACK_SIZE();
            if (LPC_UNLIKELY(stack_size < static_cast<std::size_t>(fp->stack_top) + argc)) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "call stack underflow";
                if (catch_ctx.active) throw CatchSignal(); return e;
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
            need_rebind = true;
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::IncLocal || op == Op::DecLocal)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            Value &slot = value_stack_[fp->base + idx];
            if (LPC_LIKELY(slot.IsInt64())) {
                slot = MakeI64(GetI64(slot) + (op == Op::IncLocal ? 1 : -1));
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
            if (slot.IsInt64()) {
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
            if (LPC_LIKELY(slot.IsInt64())) {
                slot = MakeI64(GetI64(slot) + 1);
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                lhs = MakeI64(lhs.AsI64() + rhs.AsI64());
                fast_handled = true;
            } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                lhs = Value::FromF64(lhs.AsF64() + rhs.AsF64());
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
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
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    lhs = MakeI64(lhs.AsI64() - rhs.AsI64());
                    LPC_DISCARD();
                    fast_handled = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    lhs = Value::FromF64(lhs.AsF64() - rhs.AsF64());
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Mul)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    lhs = MakeI64(lhs.AsI64() * rhs.AsI64());
                    LPC_DISCARD();
                    fast_handled = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    lhs = Value::FromF64(lhs.AsF64() * rhs.AsF64());
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Div)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    if (LPC_LIKELY(rhs.AsI64() != 0)) {
                        lhs = MakeI64(lhs.AsI64() / rhs.AsI64());
                        LPC_DISCARD();
                        fast_handled = true;
                    }
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    if (LPC_LIKELY(rhs.AsF64() != 0.0)) {
                        lhs = Value::FromF64(lhs.AsF64() / rhs.AsF64());
                        LPC_DISCARD();
                        fast_handled = true;
                    }
                }
            }
        } else if (LPC_LIKELY(op == Op::Eq)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                bool cmp_ok = false;
                bool eq = false;
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    eq = lhs.AsI64() == rhs.AsI64();
                    cmp_ok = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    eq = lhs.AsF64() == rhs.AsF64();
                    cmp_ok = true;
                }
                if (cmp_ok) {
                    LPC_DISCARD();
                    if (LPC_LIKELY(fp->ip < code_size)) {
                        const Op next = static_cast<Op>(code[fp->ip]);
                        if (next == Op::LogicNot) {
                            if (LPC_LIKELY(fp->ip + 2 <= code_size && static_cast<Op>(code[fp->ip + 1]) == Op::Pop)) {
                                LPC_DISCARD();
                                fp->ip += 2;
                            } else if (LPC_LIKELY(fp->ip + 4 <= code_size)) {
                                const Op next2 = static_cast<Op>(code[fp->ip + 1]);
                                if (next2 == Op::JumpIfFalse || next2 == Op::JumpIfTrue) {
                                    const std::uint8_t *jp = code + fp->ip + 2;
                                    const std::int16_t rel = static_cast<std::int16_t>(
                                        static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                                    const bool should_jump = (next2 == Op::JumpIfFalse) ? eq : !eq;
                                    LPC_DISCARD();
                                    fp->ip += 4;
                                    if (should_jump) {
                                        fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                                    }
                                } else {
                                    ++fp->ip;
                                    lhs = Value::FromI64(eq ? 0 : 1);
                                }
                            } else {
                                ++fp->ip;
                                lhs = Value::FromI64(eq ? 0 : 1);
                            }
                        } else if ((next == Op::JumpIfFalse || next == Op::JumpIfTrue) && fp->ip + 3 <= code_size) {
                            const std::uint8_t *jp = code + fp->ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (next == Op::JumpIfFalse) ? !eq : eq;
                            LPC_DISCARD();
                            fp->ip += 3;
                            if (should_jump) {
                                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                            }
                        } else {
                            lhs = Value::FromI64(eq ? 1 : 0);
                        }
                    } else {
                        lhs = Value::FromI64(eq ? 1 : 0);
                    }
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Neq)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                bool cmp_ok = false;
                bool neq = false;
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    neq = lhs.AsI64() != rhs.AsI64();
                    cmp_ok = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    neq = lhs.AsF64() != rhs.AsF64();
                    cmp_ok = true;
                }
                if (cmp_ok) {
                    LPC_DISCARD();
                    if (LPC_LIKELY(fp->ip < code_size)) {
                        const Op next = static_cast<Op>(code[fp->ip]);
                        if (next == Op::LogicNot) {
                            if (LPC_LIKELY(fp->ip + 2 <= code_size && static_cast<Op>(code[fp->ip + 1]) == Op::Pop)) {
                                LPC_DISCARD();
                                fp->ip += 2;
                            } else if (LPC_LIKELY(fp->ip + 4 <= code_size)) {
                                const Op next2 = static_cast<Op>(code[fp->ip + 1]);
                                if (next2 == Op::JumpIfFalse || next2 == Op::JumpIfTrue) {
                                    const std::uint8_t *jp = code + fp->ip + 2;
                                    const std::int16_t rel = static_cast<std::int16_t>(
                                        static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                                    const bool should_jump = (next2 == Op::JumpIfFalse) ? neq : !neq;
                                    LPC_DISCARD();
                                    fp->ip += 4;
                                    if (should_jump) {
                                        fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                                    }
                                } else {
                                    ++fp->ip;
                                    lhs = Value::FromI64(neq ? 0 : 1);
                                }
                            } else {
                                ++fp->ip;
                                lhs = Value::FromI64(neq ? 0 : 1);
                            }
                        } else if ((next == Op::JumpIfFalse || next == Op::JumpIfTrue) && fp->ip + 3 <= code_size) {
                            const std::uint8_t *jp = code + fp->ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (next == Op::JumpIfFalse) ? !neq : neq;
                            LPC_DISCARD();
                            fp->ip += 3;
                            if (should_jump) {
                                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                            }
                        } else {
                            lhs = Value::FromI64(neq ? 1 : 0);
                        }
                    } else {
                        lhs = Value::FromI64(neq ? 1 : 0);
                    }
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Lt)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                bool cmp_ok = false;
                bool cond = false;
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    cond = lhs.AsI64() < rhs.AsI64();
                    cmp_ok = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    cond = lhs.AsF64() < rhs.AsF64();
                    cmp_ok = true;
                }
                if (cmp_ok) {
                    LPC_DISCARD();
                    if (LPC_LIKELY(fp->ip < code_size)) {
                        const Op next = static_cast<Op>(code[fp->ip]);
                        if (next == Op::LogicNot) {
                            if (LPC_LIKELY(fp->ip + 2 <= code_size && static_cast<Op>(code[fp->ip + 1]) == Op::Pop)) {
                                LPC_DISCARD();
                                fp->ip += 2;
                            } else {
                                ++fp->ip;
                                lhs = Value::FromI64(cond ? 0 : 1);
                            }
                        } else if ((next == Op::JumpIfFalse || next == Op::JumpIfTrue) && fp->ip + 3 <= code_size) {
                            const std::uint8_t *jp = code + fp->ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (next == Op::JumpIfFalse) ? !cond : cond;
                            LPC_DISCARD();
                            fp->ip += 3;
                            if (should_jump) {
                                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                            }
                        } else {
                            lhs = Value::FromI64(cond ? 1 : 0);
                        }
                    } else {
                        lhs = Value::FromI64(cond ? 1 : 0);
                    }
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Lte)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                bool cmp_ok = false;
                bool cond = false;
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    cond = lhs.AsI64() <= rhs.AsI64();
                    cmp_ok = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    cond = lhs.AsF64() <= rhs.AsF64();
                    cmp_ok = true;
                }
                if (cmp_ok) {
                    LPC_DISCARD();
                    if (LPC_LIKELY(fp->ip < code_size)) {
                        const Op next = static_cast<Op>(code[fp->ip]);
                        if (next == Op::LogicNot) {
                            if (LPC_LIKELY(fp->ip + 2 <= code_size && static_cast<Op>(code[fp->ip + 1]) == Op::Pop)) {
                                LPC_DISCARD();
                                fp->ip += 2;
                            } else {
                                ++fp->ip;
                                lhs = Value::FromI64(cond ? 0 : 1);
                            }
                        } else if ((next == Op::JumpIfFalse || next == Op::JumpIfTrue) && fp->ip + 3 <= code_size) {
                            const std::uint8_t *jp = code + fp->ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (next == Op::JumpIfFalse) ? !cond : cond;
                            LPC_DISCARD();
                            fp->ip += 3;
                            if (should_jump) {
                                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                            }
                        } else {
                            lhs = Value::FromI64(cond ? 1 : 0);
                        }
                    } else {
                        lhs = Value::FromI64(cond ? 1 : 0);
                    }
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Gt)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                bool cmp_ok = false;
                bool cond = false;
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    cond = lhs.AsI64() > rhs.AsI64();
                    cmp_ok = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    cond = lhs.AsF64() > rhs.AsF64();
                    cmp_ok = true;
                }
                if (cmp_ok) {
                    LPC_DISCARD();
                    if (LPC_LIKELY(fp->ip < code_size)) {
                        const Op next = static_cast<Op>(code[fp->ip]);
                        if (next == Op::LogicNot) {
                            if (LPC_LIKELY(fp->ip + 2 <= code_size && static_cast<Op>(code[fp->ip + 1]) == Op::Pop)) {
                                LPC_DISCARD();
                                fp->ip += 2;
                            } else {
                                ++fp->ip;
                                lhs = Value::FromI64(cond ? 0 : 1);
                            }
                        } else if ((next == Op::JumpIfFalse || next == Op::JumpIfTrue) && fp->ip + 3 <= code_size) {
                            const std::uint8_t *jp = code + fp->ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (next == Op::JumpIfFalse) ? !cond : cond;
                            LPC_DISCARD();
                            fp->ip += 3;
                            if (should_jump) {
                                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                            }
                        } else {
                            lhs = Value::FromI64(cond ? 1 : 0);
                        }
                    } else {
                        lhs = Value::FromI64(cond ? 1 : 0);
                    }
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Gte)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                bool cmp_ok = false;
                bool cond = false;
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    cond = lhs.AsI64() >= rhs.AsI64();
                    cmp_ok = true;
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    cond = lhs.AsF64() >= rhs.AsF64();
                    cmp_ok = true;
                }
                if (cmp_ok) {
                    LPC_DISCARD();
                    if (LPC_LIKELY(fp->ip < code_size)) {
                        const Op next = static_cast<Op>(code[fp->ip]);
                        if (next == Op::LogicNot) {
                            if (LPC_LIKELY(fp->ip + 2 <= code_size && static_cast<Op>(code[fp->ip + 1]) == Op::Pop)) {
                                LPC_DISCARD();
                                fp->ip += 2;
                            } else {
                                ++fp->ip;
                                lhs = Value::FromI64(cond ? 0 : 1);
                            }
                        } else if ((next == Op::JumpIfFalse || next == Op::JumpIfTrue) && fp->ip + 3 <= code_size) {
                            const std::uint8_t *jp = code + fp->ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (next == Op::JumpIfFalse) ? !cond : cond;
                            LPC_DISCARD();
                            fp->ip += 3;
                            if (should_jump) {
                                fp->ip = static_cast<std::uint32_t>(static_cast<std::int64_t>(fp->ip) + rel);
                            }
                        } else {
                            lhs = Value::FromI64(cond ? 1 : 0);
                        }
                    } else {
                        lhs = Value::FromI64(cond ? 1 : 0);
                    }
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                result = MakeI64(is_sub ? lhs.AsI64() - rhs.AsI64() : lhs.AsI64() + rhs.AsI64());
            } else if (!is_sub && LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
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
        } else if (LPC_LIKELY(op == Op::BitAndLocalIConstToLocal || op == Op::BitOrLocalIConstToLocal || op == Op::BitXorLocalIConstToLocal)) {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated bitwise local operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "bitwise local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = iconst_values[rhs_idx];
            Value result;
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                const std::uint64_t a_p = lhs.bits_ & Value::kPayloadMask;
                const std::uint64_t b_p = rhs.bits_ & Value::kPayloadMask;
                std::uint64_t r_p;
                if (op == Op::BitAndLocalIConstToLocal) r_p = a_p & b_p;
                else if (op == Op::BitOrLocalIConstToLocal) r_p = a_p | b_p;
                else r_p = a_p ^ b_p;
                result.bits_ = Value::kInt64TagBits | r_p;
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const std::int64_t b = rhs.IsInt64() ? GetI64(rhs) : 0;
                std::int64_t r = 0;
                if (op == Op::BitAndLocalIConstToLocal) r = 0 & b;
                else if (op == Op::BitOrLocalIConstToLocal) r = 0 | b;
                else r = 0 ^ b;
                result = MakeI64(r);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "bitwise requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::ShlLocalIConstToLocal || op == Op::ShrLocalIConstToLocal)) {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated shift local operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "shift local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = iconst_values[rhs_idx];
            Value result = Value::Nil();
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                const std::int64_t a = lhs.AsI64();
                const std::int64_t b = rhs.AsI64();
                result = MakeI64(op == Op::ShlLocalIConstToLocal ? a << b : a >> b);
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const std::int64_t b = rhs.IsInt64() ? GetI64(rhs) : 0;
                result = MakeI64(op == Op::ShlLocalIConstToLocal ? 0 << b : 0 >> b);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "shift requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            fast_handled = true;
        } else if (LPC_LIKELY(op == Op::AddLocalFConstToLocal || op == Op::SubLocalFConstToLocal ||
                                  op == Op::MulLocalFConstToLocal || op == Op::DivLocalFConstToLocal)) {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated float local operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= fconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "float local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = fconst_values[rhs_idx];
            Value result = Value::Nil();
            if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                result = Value::FromF64(r);
            } else if (LPC_LIKELY(lhs.IsFloat64())) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                result = Value::FromF64(r);
            } else if (LPC_UNLIKELY(lhs.IsInt64())) {
                const double a = static_cast<double>(GetI64(lhs));
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else { if (LPC_UNLIKELY(b == 0.0)) { RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero"; if (catch_ctx.active) throw CatchSignal(); return e; } r = a / b; }
                result = Value::FromF64(r);
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const double a = 0.0;
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else { r = a / b; }
                result = Value::FromF64(r);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "float arithmetic unsupported type";
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
                    LPC_COMMIT_SP();
                    std::string s = ResolveObjRefStringOnly(container);
                    if (key_i < 0 || static_cast<std::size_t>(key_i) >= s.size()) {
                        RuntimeError e; e.code = RuntimeErrorCode::BoundsError; e.message = "string index out of range";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    std::string ch(1, s[static_cast<std::size_t>(key_i)]);
                    elem = InternString(ch);
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
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, elem))) {
                result = MakeI64(lhs.AsI64() + elem.AsI64());
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || elem.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(elem, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
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
            if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
                result = MakeI64(GetI64(lhs) + GetI64(rhs));
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
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
            if (LPC_LIKELY(inc_slot.IsInt64())) {
                inc_slot = MakeI64(GetI64(inc_slot) + 1);
            } else if (inc_slot.IsNil()) {
                inc_slot = Value::FromI64(1);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "IncLocal requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }

            const Value cmp_lhs = value_stack_[fp->base + cmp_lhs_idx];
            const Value cmp_rhs = value_stack_[fp->base + cmp_rhs_idx];
            bool cond = false;
            if (LPC_LIKELY(cmp_lhs.IsInt64() && cmp_rhs.IsInt64())) {
                cond = GetI64(cmp_lhs) < GetI64(cmp_rhs);
            } else if (LPC_UNLIKELY(cmp_lhs.IsFloat64() || cmp_rhs.IsFloat64())) {
                if (LPC_UNLIKELY(!(cmp_lhs.IsFloat64() || cmp_lhs.IsInt64()) ||
                                 !(cmp_rhs.IsFloat64() || cmp_rhs.IsInt64()))) {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                const double a = cmp_lhs.IsFloat64() ? cmp_lhs.AsF64() : static_cast<double>(GetI64(cmp_lhs));
                const double b = cmp_rhs.IsFloat64() ? cmp_rhs.AsF64() : static_cast<double>(GetI64(cmp_rhs));
                cond = a < b;
            } else if (LPC_UNLIKELY(cmp_lhs.IsNil() || cmp_rhs.IsNil())) {
                cond = false;
            } else if (LPC_UNLIKELY(cmp_lhs.IsObjRef() || cmp_rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string a = ResolveString(cmp_lhs);
                std::string b = ResolveString(cmp_rhs);
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
            if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
                cond = op == Op::JumpIfLocalLtFalse
                    ? GetI64(lhs) < GetI64(rhs)
                    : GetI64(lhs) <= GetI64(rhs);
            } else if (LPC_UNLIKELY(lhs.IsFloat64() || rhs.IsFloat64())) {
                if (LPC_UNLIKELY(!(lhs.IsFloat64() || lhs.IsInt64()) ||
                                 !(rhs.IsFloat64() || rhs.IsInt64()))) {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "compare op unsupported types";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                const double a = lhs.IsFloat64() ? lhs.AsF64() : static_cast<double>(GetI64(lhs));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : static_cast<double>(GetI64(rhs));
                cond = op == Op::JumpIfLocalLtFalse ? a < b : a <= b;
            } else if (LPC_UNLIKELY(lhs.IsNil() || rhs.IsNil())) {
                cond = false;
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string a = ResolveString(lhs);
                std::string b = ResolveString(rhs);
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
            bool jump;
            const ValueTag tag = cond.Tag();
            if (LPC_LIKELY(tag == ValueTag::Int64)) {
                jump = cond.AsI64() == 0;
            } else if (LPC_LIKELY(tag == ValueTag::Bool)) {
                jump = !cond.AsBool();
            } else if (tag == ValueTag::Nil) {
                jump = true;
            } else if (tag == ValueTag::Float64) {
                jump = cond.AsF64() == 0.0;
            } else if (tag == ValueTag::ObjRef) {
                jump = cond.AsObj() == 0;
            } else {
                jump = false;
            }
            if (jump) {
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
            bool jump;
            const ValueTag tag = cond.Tag();
            if (LPC_LIKELY(tag == ValueTag::Int64)) {
                jump = cond.AsI64() != 0;
            } else if (LPC_LIKELY(tag == ValueTag::Bool)) {
                jump = cond.AsBool();
            } else if (tag == ValueTag::Nil) {
                jump = false;
            } else if (tag == ValueTag::Float64) {
                jump = cond.AsF64() != 0.0;
            } else if (tag == ValueTag::ObjRef) {
                jump = cond.AsObj() != 0;
            } else {
                jump = true;
            }
            if (jump) {
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
        } else if (LPC_LIKELY(op == Op::Return)) {
#ifdef NDEBUG
            const std::uint32_t base = fp->base;
            if (LPC_STACK_SIZE() <= fp->stack_top) {
                last_result_ = Value::Nil();
            } else {
                last_result_ = LPC_POP();
            }
            const bool returning_version_pinned = fp->version_pinned;
            std::uint64_t returning_version_id = 0;
            std::string returning_module_name;
            if (returning_version_pinned) {
                returning_version_id = fp->module_version_id;
                returning_module_name = std::move(fp->module_name);
            }
            LPC_COMMIT_SP();
            frames_.pop_back();
            if (returning_version_pinned && returning_version_id != 0) {
                RuntimeError unpin_err = hot_reload_manager_.UnpinVersion(returning_module_name, returning_version_id);
                if (!unpin_err.ok()) {
                    if (catch_ctx.active) throw CatchSignal();
                    return unpin_err;
                }
            }
            LPC_COMMIT_SP();
            if (StackSize() > base) {
                StackResize(base);
            }
            LPC_LOAD_SP();
            if (!frames_.empty()) {
                LPC_PUSH(last_result_);
                fp = &frames_.back();
            }
            need_rebind = true;
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::LogicNot)) {
#ifdef NDEBUG
            if (LPC_LIKELY(fp->ip < code_size && static_cast<Op>(code[fp->ip]) == Op::Pop)) {
                LPC_DISCARD();
                ++fp->ip;
            } else {
                Value v = LPC_POP();
                std::int64_t out = 0;
                switch (v.Tag()) {
                case ValueTag::Nil: out = 1; break;
                case ValueTag::Bool: out = v.AsBool() ? 0 : 1; break;
                case ValueTag::Int64: out = (v.AsI64() == 0) ? 1 : 0; break;
                case ValueTag::Float64: out = (v.AsF64() == 0.0) ? 1 : 0; break;
                case ValueTag::ObjRef: out = (v.AsObj() == 0) ? 1 : 0; break;
                case ValueTag::Closure:
                case ValueTag::BoxedInt:
                    out = 0;
                    break;
                }
                LPC_PUSH(Value::FromI64(out));
            }
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::LoadClassField)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            const std::uint16_t field_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const Value clazz = LPC_POP();
            const auto raw = clazz.AsObj();
            const std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            LPC_PUSH(class_fields_[cls_id - 1].At(field_idx));
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::LoadLocalClassField)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t obj_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t field_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const Value clazz = value_stack_[fp->base + obj_idx];
            const auto raw = clazz.AsObj();
            const std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            LPC_PUSH(class_fields_[cls_id - 1].At(field_idx));
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::SetClassField)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            const std::uint16_t field_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const Value clazz = LPC_POP();
            const Value val = LPC_POP();
            const auto raw = clazz.AsObj();
            const std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            class_fields_[cls_id - 1].Set(field_idx, val);
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::SetClassFieldLocalFromLocal)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t obj_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t val_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t field_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            const Value clazz = value_stack_[fp->base + obj_idx];
            const Value val = value_stack_[fp->base + val_idx];
            const auto raw = clazz.AsObj();
            const std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            class_fields_[cls_id - 1].Set(field_idx, val);
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::AddLocalClassFieldToLocal)) {
#ifdef NDEBUG
            if (LPC_UNLIKELY(fp->ip + 8 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated AddLocalClassFieldToLocal operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 8;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t obj_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            const std::uint16_t field_idx = static_cast<std::uint16_t>(p[6]) | (static_cast<std::uint16_t>(p[7]) << 8);
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value clazz = value_stack_[fp->base + obj_idx];
            const auto raw = clazz.AsObj();
            const std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            const Value rhs = class_fields_[cls_id - 1].At(field_idx);
            Value result = Value::Nil();
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                result = MakeI64(lhs.AsI64() + rhs.AsI64());
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
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
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::AddLocalTwoClassFieldsToLocal)) {
#ifdef NDEBUG
            if (LPC_UNLIKELY(fp->ip + 10 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated AddLocalTwoClassFieldsToLocal operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 10;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t obj_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            const std::uint16_t field1_idx = static_cast<std::uint16_t>(p[6]) | (static_cast<std::uint16_t>(p[7]) << 8);
            const std::uint16_t field2_idx = static_cast<std::uint16_t>(p[8]) | (static_cast<std::uint16_t>(p[9]) << 8);
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value clazz = value_stack_[fp->base + obj_idx];
            const auto raw = clazz.AsObj();
            const std::size_t cls_id = static_cast<std::size_t>(raw - kClassBase);
            const LpcClass &fields = class_fields_[cls_id - 1];
            const Value f1 = fields.At(field1_idx);
            const Value f2 = fields.At(field2_idx);
            if (LPC_LIKELY(lhs.IsInlineInt64() && f1.IsInlineInt64() && f2.IsInlineInt64())) {
                value_stack_[fp->base + dst_idx] = MakeI64(lhs.AsI64() + f1.AsI64() + f2.AsI64());
            } else {
                Value rhs = Value::Nil();
                if (LPC_LIKELY(Value::BothInlineInt64(f1, f2))) {
                    rhs = MakeI64(f1.AsI64() + f2.AsI64());
                } else if (LPC_UNLIKELY(f1.IsObjRef() || f2.IsObjRef())) {
                    LPC_COMMIT_SP();
                    std::string buf_a, buf_b;
                    std::string_view sv_a = ResolveStringView(f1, buf_a);
                    std::string_view sv_b = ResolveStringView(f2, buf_b);
                    std::string joined;
                    joined.reserve(sv_a.size() + sv_b.size());
                    joined.append(sv_a);
                    joined.append(sv_b);
                    rhs = InternString(joined);
                } else if (LPC_UNLIKELY(f1.IsFloat64() || f2.IsFloat64())) {
                    const double a = f1.IsFloat64() ? f1.AsF64() : (f1.IsNil() ? 0.0 : static_cast<double>(GetI64(f1)));
                    const double b = f2.IsFloat64() ? f2.AsF64() : (f2.IsNil() ? 0.0 : static_cast<double>(GetI64(f2)));
                    rhs = Value::FromF64(a + b);
                } else if (LPC_UNLIKELY(f1.IsNil() || f2.IsNil())) {
                    const std::int64_t a = f1.IsNil() ? 0 : GetI64(f1);
                    const std::int64_t b = f2.IsNil() ? 0 : GetI64(f2);
                    rhs = MakeI64(a + b);
                } else {
                    RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "Add unsupported types";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }

                Value result = Value::Nil();
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    result = MakeI64(lhs.AsI64() + rhs.AsI64());
                } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                    LPC_COMMIT_SP();
                    std::string buf_a, buf_b;
                    std::string_view sv_a = ResolveStringView(lhs, buf_a);
                    std::string_view sv_b = ResolveStringView(rhs, buf_b);
                    std::string joined;
                    joined.reserve(sv_a.size() + sv_b.size());
                    joined.append(sv_a);
                    joined.append(sv_b);
                    result = InternString(joined);
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
            }
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::Inc)) {
#ifdef NDEBUG
            Value &v = LPC_TOP();
            if (v.IsInlineInt64()) {
                v = MakeI64(v.AsI64() + 1);
                fast_handled = true;
            }
#endif
        } else if (LPC_LIKELY(op == Op::Dec)) {
#ifdef NDEBUG
            Value &v = LPC_TOP();
            if (v.IsInlineInt64()) {
                v = MakeI64(v.AsI64() - 1);
                fast_handled = true;
            }
#endif
        } else if (LPC_LIKELY(op == Op::Neg)) {
#ifdef NDEBUG
            Value &v = LPC_TOP();
            if (v.IsInlineInt64()) {
                v = MakeI64(-v.AsI64());
                fast_handled = true;
            } else if (LPC_LIKELY(v.IsInlineFloat64())) {
                v = Value::FromF64(-v.AsF64());
                fast_handled = true;
            }
#endif
        } else if (LPC_LIKELY(op == Op::LoadSConst)) {
#ifdef NDEBUG
            {
                const std::uint8_t *p = code + fp->ip;
                fp->ip += 2;
                std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
                LPC_PUSH(sconst_values[idx]);
            }
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::LoadFConst)) {
#ifdef NDEBUG
            {
                const std::uint8_t *p = code + fp->ip;
                fp->ip += 2;
                std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
                LPC_PUSH(fconst_values[idx]);
            }
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::CallIntrinsic)) {
#ifdef NDEBUG
            if (LPC_LIKELY(fp->ip + 3 <= code_size)) {
                const std::uint8_t *p = code + fp->ip;
                const std::uint16_t efun_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
                const std::uint8_t argc = p[2];
                const auto efun = static_cast<Efun>(efun_idx);
                if (argc == 1 && LPC_STACK_SZ() > fp->stack_top && efun == Efun::ToFloat) {
                    fp->ip += 3;
                    Value v = LPC_POP();
                    if (v.IsInlineInt64()) {
                        LPC_PUSH(Value::FromF64(static_cast<double>(v.AsI64())));
                    } else if (v.IsFloat64()) {
                        LPC_PUSH(v);
                    } else {
                        LPC_PUSH(Value::FromF64(0.0));
                    }
                    fast_handled = true;
                } else if (argc == 1 && LPC_STACK_SZ() > fp->stack_top && efun == Efun::ToInt) {
                    fp->ip += 3;
                    Value v = LPC_POP();
                    if (v.IsInlineInt64()) {
                        LPC_PUSH(v);
                    } else if (v.IsFloat64()) {
                        LPC_PUSH(MakeI64(static_cast<std::int64_t>(v.AsF64())));
                    } else {
                        LPC_PUSH(MakeI64(0));
                    }
                    fast_handled = true;
                }
            }
#endif
        } else if (LPC_LIKELY(op == Op::NewClass)) {
#ifdef NDEBUG
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 2;
            const std::uint16_t class_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            LPC_PUSH(AllocateClassHandle(class_idx));
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::Mod)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    if (LPC_LIKELY(rhs.AsI64() != 0)) {
                        lhs = MakeI64(lhs.AsI64() % rhs.AsI64());
                        LPC_DISCARD();
                        fast_handled = true;
                    }
                } else if (LPC_LIKELY(Value::BothInlineFloat64(lhs, rhs))) {
                    if (LPC_LIKELY(rhs.AsF64() != 0.0)) {
                        lhs = Value::FromF64(std::fmod(lhs.AsF64(), rhs.AsF64()));
                        LPC_DISCARD();
                        fast_handled = true;
                    }
                }
            }
        } else if (LPC_LIKELY(op == Op::BitAnd || op == Op::BitOr || op == Op::BitXor)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    std::int64_t result;
                    if (op == Op::BitAnd) result = lhs.AsI64() & rhs.AsI64();
                    else if (op == Op::BitOr) result = lhs.AsI64() | rhs.AsI64();
                    else result = lhs.AsI64() ^ rhs.AsI64();
                    lhs = MakeI64(result);
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::Shl || op == Op::Shr)) {
            if (LPC_STACK_SZ() >= fp->stack_top + 2) {
                const Value rhs = LPC_TOP();
                Value &lhs = LPC_PEEK(1);
                if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                    std::int64_t result;
                    if (op == Op::Shl) result = lhs.AsI64() << rhs.AsI64();
                    else result = lhs.AsI64() >> rhs.AsI64();
                    lhs = MakeI64(result);
                    LPC_DISCARD();
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::BitNot)) {
            if (LPC_STACK_SZ() > fp->stack_top) {
                Value &v = LPC_TOP();
                if (v.IsInlineInt64()) {
                    v = MakeI64(~v.AsI64());
                    fast_handled = true;
                }
            }
        } else if (LPC_LIKELY(op == Op::LoadUpvalue)) {
#ifdef NDEBUG
            {
                const std::uint8_t *p = code + fp->ip;
                fp->ip += 2;
                const std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
                LPC_PUSH(closures_[fp->closure_slot].Data()[idx]);
                fast_handled = true;
            }
#endif
        } else if (LPC_LIKELY(op == Op::StoreUpvalue)) {
#ifdef NDEBUG
            {
                const std::uint8_t *p = code + fp->ip;
                fp->ip += 2;
                const std::uint16_t idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
                closures_[fp->closure_slot].Data()[idx] = LPC_TOP();
                LPC_DISCARD();
                fast_handled = true;
            }
#endif
        } else if (LPC_LIKELY(op == Op::AddLocalToUpvalueAndLoad)) {
#ifdef NDEBUG
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated AddLocalToUpvalueAndLoad operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t up_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            Value lhs = closures_[fp->closure_slot].Data()[up_idx];
            Value rhs = value_stack_[fp->base + local_idx];
            Value result = Value::Nil();
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                result = MakeI64(lhs.AsI64() + rhs.AsI64());
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
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
            closures_[fp->closure_slot].Data()[up_idx] = result;
            LPC_PUSH(result);
            fast_handled = true;
#endif
        } else if (LPC_LIKELY(op == Op::CallValue)) {
#ifdef NDEBUG
            {
                const std::uint8_t *p = code + fp->ip;
                fp->ip += 2;
                const std::uint16_t argc = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
                Value callee_val = LPC_POP();
                const auto &functions = BoundChunk().functions;
                std::uint16_t fid = 0;
                std::uint64_t callee_version = fp->module_version_id;
                std::uint32_t closure_slot = 0;
                if (IsFuncObjRef(callee_val)) {
                    fid = static_cast<std::uint16_t>(DecodeFuncId(callee_val) - 1);
                } else if (callee_val.IsClosure()) {
                    const std::uint32_t cid = callee_val.ClosureId();
                    const LpcClosure &cl = closures_[cid - 1];
                    fid = cl.FuncId();
                    callee_version = cl.ModuleVersionId();
                    closure_slot = cid - 1;
                } else {
                    LPC_PUSH(callee_val);
                    goto lpc_call_value_slow;
                }
                const FunctionProto &callee = functions[fid];
                if (LPC_UNLIKELY(argc != callee.arity)) {
                    RuntimeError e; e.code = RuntimeErrorCode::ArityMismatch; e.message = "CallValue argc mismatch";
                    if (catch_ctx.active) throw CatchSignal(); return e;
                }
                RuntimeError stack_err = ensure_call_stack_room();
                if (LPC_UNLIKELY(!stack_err.ok())) {
                    return stack_err;
                }
                const std::uint32_t callee_base = static_cast<std::uint32_t>(LPC_STACK_SIZE() - argc);
                extend_call_locals(callee, argc);
                RuntimeError call_err = push_frame(
                    fid, callee.code_start, callee_base,
                    callee_base + callee.nlocals, fp->object_id,
                    callee_version, fp->module_name,
                    callee_version == fp->module_version_id, closure_slot);
                if (!call_err.ok()) { return call_err; }
                fp = &frames_.back();
                need_rebind = true;
                fast_handled = true;
            }
#endif
        } else if (LPC_LIKELY(op == Op::Pop)) {
            LPC_DISCARD();
            fast_handled = true;
        }

        if (!fast_handled) {
        switch (op) {

        lpc_add_slow:
        case Op::Add: {
            if (LPC_STACK_SZ() < fp->stack_top + 2) {
                RuntimeError e; e.code = RuntimeErrorCode::StackUnderflow; e.message = "Add requires two operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            Value rhs = LPC_POP();
            Value lhs = LPC_POP();
            if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
                LPC_PUSH(MakeI64(GetI64(lhs) + GetI64(rhs)));
            } else if (LPC_UNLIKELY(lhs.IsObjRef() || rhs.IsObjRef())) {
                LPC_COMMIT_SP();
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string result;
                result.reserve(sv_a.size() + sv_b.size());
                result.append(sv_a);
                result.append(sv_b);
                LPC_PUSH(InternString(result));
                LPC_COMMIT_SP();
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
            } else if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
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
            } else if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
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
            } else if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
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
            if (lhs.IsInt64() && rhs.IsInt64()) {
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
            if (v.IsInt64()) {
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
            if (v.IsInt64()) {
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
            if (v.IsInt64()) {
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

        case Op::BitAndLocalIConstToLocal:
        case Op::BitOrLocalIConstToLocal:
        case Op::BitXorLocalIConstToLocal:
        case Op::ShlLocalIConstToLocal:
        case Op::ShrLocalIConstToLocal: {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated bitwise local operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
#ifndef NDEBUG
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= iconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "bitwise local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
#endif
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = iconst_values[rhs_idx];
            Value result = Value::Nil();
            if (LPC_LIKELY(Value::BothInlineInt64(lhs, rhs))) {
                if (op == Op::BitAndLocalIConstToLocal || op == Op::BitOrLocalIConstToLocal || op == Op::BitXorLocalIConstToLocal) {
                    std::uint64_t a_p = lhs.bits_ & Value::kPayloadMask;
                    std::uint64_t b_p = rhs.bits_ & Value::kPayloadMask;
                    std::uint64_t r_p;
                    if (op == Op::BitAndLocalIConstToLocal) r_p = a_p & b_p;
                    else if (op == Op::BitOrLocalIConstToLocal) r_p = a_p | b_p;
                    else r_p = a_p ^ b_p;
                    result.bits_ = Value::kInt64TagBits | r_p;
                } else {
                    const std::int64_t a = lhs.AsI64();
                    const std::int64_t b = rhs.AsI64();
                    if (op == Op::ShlLocalIConstToLocal) result = MakeI64(a << b);
                    else result = MakeI64(a >> b);
                }
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const std::int64_t a = 0;
                const std::int64_t b = rhs.IsInt64() ? GetI64(rhs) : (rhs.IsNil() ? 0 : 0);
                std::int64_t r = 0;
                if (op == Op::BitAndLocalIConstToLocal) r = a & b;
                else if (op == Op::BitOrLocalIConstToLocal) r = a | b;
                else if (op == Op::BitXorLocalIConstToLocal) r = a ^ b;
                else if (op == Op::ShlLocalIConstToLocal) r = a << b;
                else r = a >> b;
                result = MakeI64(r);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "bitwise/shift requires Int64";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            break;
        }

        case Op::AddLocalFConstToLocal:
        case Op::SubLocalFConstToLocal:
        case Op::MulLocalFConstToLocal:
        case Op::DivLocalFConstToLocal: {
            if (LPC_UNLIKELY(fp->ip + 6 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated float local operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 6;
            const std::uint16_t dst_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t lhs_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            const std::uint16_t rhs_idx = static_cast<std::uint16_t>(p[4]) | (static_cast<std::uint16_t>(p[5]) << 8);
            if (LPC_UNLIKELY(dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || rhs_idx >= fconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "float local index out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const Value lhs = value_stack_[fp->base + lhs_idx];
            const Value rhs = fconst_values[rhs_idx];
            Value result = Value::Nil();
            if (LPC_LIKELY(lhs.IsFloat64())) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else {
                    if (LPC_UNLIKELY(b == 0.0)) {
                        RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    r = a / b;
                }
                result = Value::FromF64(r);
            } else if (LPC_UNLIKELY(lhs.IsInt64())) {
                const double a = static_cast<double>(GetI64(lhs));
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else {
                    if (LPC_UNLIKELY(b == 0.0)) {
                        RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    r = a / b;
                }
                result = Value::FromF64(r);
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const double a = 0.0;
                const double b = rhs.AsF64();
                double r;
                if (op == Op::AddLocalFConstToLocal) r = a + b;
                else if (op == Op::SubLocalFConstToLocal) r = a - b;
                else if (op == Op::MulLocalFConstToLocal) r = a * b;
                else r = a / b;
                result = Value::FromF64(r);
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "float arithmetic unsupported type";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            value_stack_[fp->base + dst_idx] = result;
            break;
        }

        case Op::LoadLocalAddFConst:
        case Op::LoadLocalSubFConst:
        case Op::LoadLocalMulFConst:
        case Op::LoadLocalDivFConst: {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated float local expr operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t fconst_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            if (LPC_UNLIKELY(local_idx >= curf.nlocals || fconst_idx >= fconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "float local expr operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value rhs = fconst_values[fconst_idx];
            if (LPC_LIKELY(lhs.IsFloat64())) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else {
                    if (LPC_UNLIKELY(b == 0.0)) {
                        RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    r = a / b;
                }
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsInt64())) {
                const double a = static_cast<double>(GetI64(lhs));
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else {
                    if (LPC_UNLIKELY(b == 0.0)) {
                        RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    r = a / b;
                }
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const double a = 0.0;
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalAddFConst) r = a + b;
                else if (op == Op::LoadLocalSubFConst) r = a - b;
                else if (op == Op::LoadLocalMulFConst) r = a * b;
                else r = a / b;
                LPC_PUSH(Value::FromF64(r));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "float arithmetic unsupported type";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            break;
        }

        case Op::LoadLocalDupAddFConst:
        case Op::LoadLocalDupSubFConst:
        case Op::LoadLocalDupMulFConst:
        case Op::LoadLocalDupDivFConst: {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "truncated float local-dup expr operands";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            const std::uint16_t local_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            const std::uint16_t fconst_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            if (LPC_UNLIKELY(local_idx >= curf.nlocals || fconst_idx >= fconst_size)) {
                RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "float local-dup expr operand out of range";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
            const Value lhs = value_stack_[fp->base + local_idx];
            const Value rhs = fconst_values[fconst_idx];
            if (LPC_LIKELY(lhs.IsFloat64())) {
                const double a = lhs.AsF64();
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else {
                    if (LPC_UNLIKELY(b == 0.0)) {
                        RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    r = a / b;
                }
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsInt64())) {
                const double a = static_cast<double>(GetI64(lhs));
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else {
                    if (LPC_UNLIKELY(b == 0.0)) {
                        RuntimeError e; e.code = RuntimeErrorCode::InvalidOperand; e.message = "division by zero";
                        if (catch_ctx.active) throw CatchSignal(); return e;
                    }
                    r = a / b;
                }
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else if (LPC_UNLIKELY(lhs.IsNil())) {
                const double a = 0.0;
                const double b = rhs.AsF64();
                double r;
                if (op == Op::LoadLocalDupAddFConst) r = a + b;
                else if (op == Op::LoadLocalDupSubFConst) r = a - b;
                else if (op == Op::LoadLocalDupMulFConst) r = a * b;
                else r = a / b;
                LPC_PUSH(lhs);
                LPC_PUSH(Value::FromF64(r));
            } else {
                RuntimeError e; e.code = RuntimeErrorCode::TypeError; e.message = "float arithmetic unsupported type";
                if (catch_ctx.active) throw CatchSignal(); return e;
            }
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
            } else if (LPC_LIKELY(lhs.IsInt64() && rhs.IsInt64())) {
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
                LPC_COMMIT_SP();
                std::string a = ResolveString(lhs);
                std::string b = ResolveString(rhs);
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

            const bool returning_version_pinned = fp->version_pinned;
            std::uint64_t returning_version_id = 0;
            std::string returning_module_name;
            if (returning_version_pinned) {
                returning_version_id = fp->module_version_id;
                returning_module_name = std::move(fp->module_name);
            }
            LPC_COMMIT_SP();
            frames_.pop_back();
            if (returning_version_pinned && returning_version_id != 0) {
                RuntimeError unpin_err = hot_reload_manager_.UnpinVersion(returning_module_name, returning_version_id);
                if (!unpin_err.ok()) {
                    if (catch_ctx.active) throw CatchSignal();
                    return unpin_err;
                }
            }

            LPC_COMMIT_SP();
            if (StackSize() > base) {
                StackResize(base);
            }
            LPC_LOAD_SP();
            if (!frames_.empty()) {
                LPC_PUSH(last_result_);
                fp = &frames_.back();
            }
#ifdef NDEBUG
            need_rebind = true;
#endif
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
            LPC_COMMIT_SP();
            if (idx >= globals->size()) {
                globals->resize(static_cast<std::size_t>(idx) + 1, Value::Nil());
            }
            (*globals)[idx] = LPC_TOP();
            LPC_DISCARD();
            LPC_COMMIT_SP();
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

        case Op::AddLocalToUpvalueAndLoad: {
            if (LPC_UNLIKELY(fp->ip + 4 > code_size)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated AddLocalToUpvalueAndLoad operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            const std::uint8_t *p = code + fp->ip;
            fp->ip += 4;
            std::uint16_t up_idx = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
            std::uint16_t local_idx = static_cast<std::uint16_t>(p[2]) | (static_cast<std::uint16_t>(p[3]) << 8);
            if (LPC_UNLIKELY(fp->closure_slot >= closures_.size() ||
                up_idx >= closures_[fp->closure_slot].UpvalueCount() ||
                local_idx >= curf.nlocals)) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "AddLocalToUpvalueAndLoad operand out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value lhs = closures_[fp->closure_slot].GetUpvalue(up_idx);
            Value rhs = value_stack_[fp->base + local_idx];
            Value result = Value::Nil();
            if (lhs.IsInt64() && rhs.IsInt64()) {
                result = MakeI64(GetI64(lhs) + GetI64(rhs));
            } else if (lhs.IsObjRef() || rhs.IsObjRef()) {
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                result = InternString(joined);
            } else if (lhs.IsFloat64() || rhs.IsFloat64()) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : (rhs.IsNil() ? 0.0 : static_cast<double>(GetI64(rhs)));
                result = Value::FromF64(a + b);
            } else if (lhs.IsNil() || rhs.IsNil()) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                result = MakeI64(a + b);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            closures_[fp->closure_slot].SetUpvalue(up_idx, result);
            LPC_PUSH(result);
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
#ifdef NDEBUG
            need_rebind = true;
#endif
            break;
        }

        lpc_call_value_slow:
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
#ifdef NDEBUG
            need_rebind = true;
#endif
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
                    LPC_COMMIT_SP();
                    StackResize(new_size);
                    LPC_LOAD_SP();
                    LPC_PUSH(Value::Nil());
                };
                const std::size_t stack_size = LPC_STACK_SIZE();
                if (argc < 2 || stack_size < argc) {
                    LPC_COMMIT_SP();
                    const std::size_t discard_n = std::min<std::size_t>(static_cast<std::size_t>(argc), StackSize());
                    fail_call_other_nil(StackSize() - discard_n);
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
                LPC_COMMIT_SP();
                StackResize(arg_base + call_argc);
                LPC_LOAD_SP();

                RuntimeError stack_err = ensure_call_stack_room();
                if (!stack_err.ok()) {
                    return stack_err;
                }

                const std::uint32_t callee_base = static_cast<std::uint32_t>(StackSize() - call_argc);
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
#ifdef NDEBUG
                need_rebind = true;
#endif
                break;
            }

            Efun efun = static_cast<Efun>(efun_idx);
            Value result = Value::Nil();
            bool intrinsic_handled = false;

            if (efun == Efun::ThisObject && argc == 0) {
                if (current_object_id_ > 0 && current_object_id_ <= objects_.size()) {
                    result = MakeObjectHandle(current_object_id_);
                } else {
                    result = Value::Nil();
                }
                intrinsic_handled = true;
            } else if (efun == Efun::Instanceof && argc == 2 && LPC_STACK_SIZE() >= fp->stack_top + 2) {
                Value rhs = LPC_POP();
                Value lhs = LPC_POP();
                if (!IsClassObjRef(lhs)) {
                    result = MakeI64(0);
                } else {
                    std::uint16_t cur = 0;
                    if (!ResolveClassTemplateIndex(lhs, &cur)) {
                        result = MakeI64(0);
                    } else {
                        const std::uint16_t tmpl_idx = cur;
                        const std::uint64_t rhs_bits = rhs.RawBits();
                        bool cache_hit = false;
                        for (std::size_t ci = 0; ci < instanceof_cache_template_idx_.size(); ++ci) {
                            if (instanceof_cache_template_idx_[ci] == tmpl_idx && instanceof_cache_target_bits_[ci] == rhs_bits) {
                                result = MakeI64(instanceof_cache_result_[ci] ? 1 : 0);
                                cache_hit = true;
                                break;
                            }
                        }
                        if (!cache_hit) {
                        std::string_view target_name;
                        bool has_target = false;
                        if (rhs.IsInt64()) {
                            std::uint16_t idx = static_cast<std::uint16_t>(GetI64(rhs));
                            if (idx < BoundChunk().classes.size()) {
                                target_name = BoundChunk().classes[idx].name;
                                has_target = true;
                            }
                        } else if (rhs.IsObjRef() && IsStringObjRefFull(rhs)) {
                            const std::uintptr_t raw = rhs.AsObj();
                            if (raw > 0 && raw < kFuncBase) {
                                if (IsSConstStringRaw(raw)) {
                                    const std::uint32_t sidx = DecodeStringIndex(raw);
                                    if (sidx < BoundChunk().sconst.size()) {
                                        target_name = BoundChunk().sconst[sidx];
                                        has_target = true;
                                    }
                                } else {
                                    const std::uint32_t hidx = DecodeStringIndex(raw);
                                    if (hidx < string_heap_.size()) {
                                        target_name = string_heap_[hidx];
                                        has_target = true;
                                    }
                                }
                            }
                        }
                        if (!has_target || target_name.empty()) {
                            result = MakeI64(0);
                        } else {
                            result = MakeI64(0);
                            while (cur != kInvalidIndex16 && cur < BoundChunk().classes.size()) {
                                if (BoundChunk().classes[cur].name == target_name) {
                                    result = MakeI64(1);
                                    break;
                                }
                                cur = BoundChunk().classes[cur].parent_class_idx;
                            }
                        }
                        const std::size_t slot = static_cast<std::size_t>(instanceof_cache_next_slot_) % instanceof_cache_template_idx_.size();
                        instanceof_cache_template_idx_[slot] = tmpl_idx;
                        instanceof_cache_target_bits_[slot] = rhs_bits;
                        instanceof_cache_result_[slot] = static_cast<std::uint8_t>(result.Tag() == ValueTag::Int64 && result.AsI64() != 0 ? 1 : 0);
                        instanceof_cache_next_slot_ = static_cast<std::uint8_t>((slot + 1) % instanceof_cache_template_idx_.size());
                        }
                    }
                }
                intrinsic_handled = true;
            } else if (argc == 1 && LPC_STACK_SIZE() > fp->stack_top) {
                Value v = LPC_POP();
                switch (efun) {
                case Efun::Typeof:
                    result = MakeI64(static_cast<std::int64_t>(v.Tag()));
                    intrinsic_handled = true;
                    break;
                case Efun::ToInt:
                    if (v.IsBool()) result = MakeI64(v.AsI64());
                    else if (v.IsInt64()) result = v;
                    else if (v.IsFloat64()) result = MakeI64(static_cast<std::int64_t>(v.AsF64()));
                    else result = MakeI64(0);
                    intrinsic_handled = true;
                    break;
                case Efun::Stringp:
                    result = MakeBool(IsStringObjRefFull(v));
                    intrinsic_handled = true;
                    break;
                case Efun::Intp:
                    result = MakeBool(v.IsInt64());
                    intrinsic_handled = true;
                    break;
                case Efun::Floatp:
                    result = MakeBool(v.IsFloat64());
                    intrinsic_handled = true;
                    break;
                case Efun::Nullp:
                    result = MakeBool(v.IsNil());
                    intrinsic_handled = true;
                    break;
                case Efun::Functionp:
                    result = MakeBool(v.IsClosure());
                    intrinsic_handled = true;
                    break;
                case Efun::ToFloat:
                    if (v.IsFloat64()) result = v;
                    else if (v.Tag() == ValueTag::Int64) result = Value::FromF64(static_cast<double>(v.AsI64()));
                    else if (v.IsBool()) result = Value::FromF64(static_cast<double>(v.AsI64()));
                    else result = Value::FromF64(0.0);
                    intrinsic_handled = true;
                    break;
                default:
                    LPC_PUSH(v);
                    break;
                }
            }

            if (!intrinsic_handled) {
                LPC_COMMIT_SP();
                result = DispatchIntrinsic(efun_idx, argc);
                LPC_LOAD_SP();
            }

            if (!EfunIsVoid(efun)) {
                if (intrinsic_handled && efun == Efun::Instanceof && result.Tag() == ValueTag::Int64 && fp->ip < code_size) {
                    bool cond = result.AsI64() != 0;
                    std::uint32_t scan_ip = fp->ip;
                    if (scan_ip < code_size && static_cast<Op>(code[scan_ip]) == Op::LogicNot) {
                        cond = !cond;
                        ++scan_ip;
                    }
                    if (scan_ip + 2 < code_size) {
                        const Op jump_op = static_cast<Op>(code[scan_ip]);
                        if (jump_op == Op::JumpIfFalse || jump_op == Op::JumpIfTrue) {
                            const std::uint8_t *jp = code + scan_ip + 1;
                            const std::int16_t rel = static_cast<std::int16_t>(
                                static_cast<std::uint16_t>(jp[0]) | (static_cast<std::uint16_t>(jp[1]) << 8));
                            const bool should_jump = (jump_op == Op::JumpIfFalse) ? !cond : cond;
                            fp->ip = scan_ip + 3;
                            if (should_jump) {
                                const std::int64_t next_ip = static_cast<std::int64_t>(fp->ip) + rel;
#ifndef NDEBUG
                                if (LPC_UNLIKELY(next_ip < static_cast<std::int64_t>(curf.code_start) ||
                                    next_ip >= static_cast<std::int64_t>(curf.code_end))) {
                                    RuntimeError e;
                                    e.code = RuntimeErrorCode::InvalidOperand;
                                    e.message = "jump target out of function range";
                                    if (catch_ctx.active) throw CatchSignal();
                                    return e;
                                }
#endif
                                fp->ip = static_cast<std::uint32_t>(next_ip);
                            }
                            break;
                        }
                    }
                }
                LPC_PUSH(result);
            }
            break;
        }

        case Op::CallVirtual: {
            const auto fail_call_virtual = [&](std::uint16_t discard_argc, const std::string &msg) -> RuntimeError {
                LPC_COMMIT_SP();
                const std::size_t discard_n = std::min<std::size_t>(static_cast<std::size_t>(discard_argc), StackSize());
                StackResize(StackSize() - discard_n);
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
#ifdef NDEBUG
            need_rebind = true;
#endif
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

        case Op::LoadLocalClassField: {
            if (fp->ip + 4 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated LoadLocalClassField operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t obj_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t field_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (obj_idx >= curf.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "LoadLocalClassField local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value clazz = value_stack_[fp->base + obj_idx];
            if (!clazz.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "LoadLocalClassField expects class object";
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

        case Op::SetClassFieldLocalFromLocal: {
            if (fp->ip + 6 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated SetClassFieldLocalFromLocal operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t obj_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t val_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t field_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (obj_idx >= curf.nlocals || val_idx >= curf.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "SetClassFieldLocalFromLocal local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value clazz = value_stack_[fp->base + obj_idx];
            Value val = value_stack_[fp->base + val_idx];
            if (!clazz.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "SetClassFieldLocalFromLocal expects class object";
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

        case Op::AddLocalClassFieldToLocal: {
            if (fp->ip + 8 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated AddLocalClassFieldToLocal operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t dst_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t lhs_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t obj_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t field_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || obj_idx >= curf.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "AddLocalClassFieldToLocal local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value clazz = value_stack_[fp->base + obj_idx];
            if (!clazz.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "AddLocalClassFieldToLocal expects class object";
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
            Value lhs = value_stack_[fp->base + lhs_idx];
            Value rhs = fields.At(field_idx);
            if (lhs.IsInt64() && rhs.IsInt64()) {
                value_stack_[fp->base + dst_idx] = MakeI64(GetI64(lhs) + GetI64(rhs));
            } else if (lhs.IsObjRef() || rhs.IsObjRef()) {
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                value_stack_[fp->base + dst_idx] = InternString(joined);
            } else if (lhs.IsFloat64() || rhs.IsFloat64()) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : (rhs.IsNil() ? 0.0 : static_cast<double>(GetI64(rhs)));
                value_stack_[fp->base + dst_idx] = Value::FromF64(a + b);
            } else if (lhs.IsNil() || rhs.IsNil()) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                value_stack_[fp->base + dst_idx] = MakeI64(a + b);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            break;
        }

        case Op::AddLocalTwoClassFieldsToLocal: {
            if (fp->ip + 10 > BoundChunk().code.size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "truncated AddLocalTwoClassFieldsToLocal operands";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            std::uint16_t dst_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t lhs_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t obj_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t field1_idx = ReadU16(BoundChunk().code, &fp->ip);
            std::uint16_t field2_idx = ReadU16(BoundChunk().code, &fp->ip);
            if (dst_idx >= curf.nlocals || lhs_idx >= curf.nlocals || obj_idx >= curf.nlocals) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "AddLocalTwoClassFieldsToLocal local index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value clazz = value_stack_[fp->base + obj_idx];
            if (!clazz.IsObjRef()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "AddLocalTwoClassFieldsToLocal expects class object";
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
            if (field1_idx >= fields.Size() || field2_idx >= fields.Size()) {
                RuntimeError e;
                e.code = RuntimeErrorCode::InvalidOperand;
                e.message = "class field index out of range";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
            Value lhs = value_stack_[fp->base + lhs_idx];
            Value f1 = fields.At(field1_idx);
            Value f2 = fields.At(field2_idx);

            Value rhs = Value::Nil();
            if (f1.IsInt64() && f2.IsInt64()) {
                rhs = MakeI64(GetI64(f1) + GetI64(f2));
            } else if (f1.IsObjRef() || f2.IsObjRef()) {
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(f1, buf_a);
                std::string_view sv_b = ResolveStringView(f2, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                rhs = InternString(joined);
            } else if (f1.IsFloat64() || f2.IsFloat64()) {
                const double a = f1.IsFloat64() ? f1.AsF64() : (f1.IsNil() ? 0.0 : static_cast<double>(GetI64(f1)));
                const double b = f2.IsFloat64() ? f2.AsF64() : (f2.IsNil() ? 0.0 : static_cast<double>(GetI64(f2)));
                rhs = Value::FromF64(a + b);
            } else if (f1.IsNil() || f2.IsNil()) {
                const std::int64_t a = f1.IsNil() ? 0 : GetI64(f1);
                const std::int64_t b = f2.IsNil() ? 0 : GetI64(f2);
                rhs = MakeI64(a + b);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }

            if (lhs.IsInt64() && rhs.IsInt64()) {
                value_stack_[fp->base + dst_idx] = MakeI64(GetI64(lhs) + GetI64(rhs));
            } else if (lhs.IsObjRef() || rhs.IsObjRef()) {
                std::string buf_a, buf_b;
                std::string_view sv_a = ResolveStringView(lhs, buf_a);
                std::string_view sv_b = ResolveStringView(rhs, buf_b);
                std::string joined;
                joined.reserve(sv_a.size() + sv_b.size());
                joined.append(sv_a);
                joined.append(sv_b);
                value_stack_[fp->base + dst_idx] = InternString(joined);
            } else if (lhs.IsFloat64() || rhs.IsFloat64()) {
                const double a = lhs.IsFloat64() ? lhs.AsF64() : (lhs.IsNil() ? 0.0 : static_cast<double>(GetI64(lhs)));
                const double b = rhs.IsFloat64() ? rhs.AsF64() : (rhs.IsNil() ? 0.0 : static_cast<double>(GetI64(rhs)));
                value_stack_[fp->base + dst_idx] = Value::FromF64(a + b);
            } else if (lhs.IsNil() || rhs.IsNil()) {
                const std::int64_t a = lhs.IsNil() ? 0 : GetI64(lhs);
                const std::int64_t b = rhs.IsNil() ? 0 : GetI64(rhs);
                value_stack_[fp->base + dst_idx] = MakeI64(a + b);
            } else {
                RuntimeError e;
                e.code = RuntimeErrorCode::TypeError;
                e.message = "Add unsupported types";
                if (catch_ctx.active) throw CatchSignal();
                return e;
            }
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
            LPC_COMMIT_SP();
            LPC_PUSH(AllocateArrayHandle(std::move(arr)));
            LPC_COMMIT_SP();
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
            LPC_COMMIT_SP();
            LPC_PUSH(AllocateMappingHandle(std::move(map)));
            LPC_COMMIT_SP();
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
            if (arrv.IsObjRef() && key.IsInt64()) {
                const std::int64_t key_i = GetI64(key);
                if (IsStringObjRef(arrv)) {
                    LPC_COMMIT_SP();
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
                    LPC_COMMIT_SP();
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
            if (key.IsInt64()) {
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
                            if (arr.At(idx).IsInt64()) {
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
            while (!pending_lifecycle_.empty()) {
                PendingLifecycleCall plc = std::move(pending_lifecycle_.back());
                pending_lifecycle_.pop_back();
                const ModuleRuntimeState *plc_ms = GetModuleState(plc.module_name);
                if (!plc_ms) break;
                auto plc_vit = plc_ms->version_runtime_data.find(plc.module_version_id);
                if (plc_vit == plc_ms->version_runtime_data.end()) break;
                const VersionRuntimeData &plc_vrd = plc_vit->second;
                if (plc.func_id >= plc_vrd.chunk.functions.size()) break;
                const FunctionProto &callee = plc_vrd.chunk.functions[plc.func_id];
                if (callee.arity != 0) break;
                RuntimeError stack_err = ensure_call_stack_room();
                if (!stack_err.ok()) return stack_err;
                LPC_COMMIT_SP();
                const std::uint32_t callee_base = static_cast<std::uint32_t>(StackSize());
                StackResize(StackSize() + callee.nlocals);
                LPC_LOAD_SP();
                RuntimeError call_err = push_frame(
                    plc.func_id,
                    callee.code_start,
                    callee_base,
                    callee_base + callee.nlocals,
                    plc.object_id,
                    plc.module_version_id,
                    plc.module_name,
                    false,
                    0);
                if (!call_err.ok()) return call_err;
                fp = &frames_.back();
#ifdef NDEBUG
                need_rebind = true;
#endif
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
                LPC_COMMIT_SP();
                std::size_t arr_id = DecodeArrayId(container);
                if (arr_id == 0 || arr_id > arrays_.size()) {
                    done = true;
                } else {
                    LpcArray &arr = arrays_[arr_id - 1];
                    std::int64_t index = iter.IsInt64() ? GetI64(iter) : 0;
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

lpc_post_dispatch:
        if (catch_ctx.active && fp->ip >= catch_ctx.end_ip) {
            if (LPC_STACK_SIZE() > catch_ctx.stack_watermark) {
                LPC_COMMIT_SP();
                StackResize(catch_ctx.stack_watermark);
                LPC_LOAD_SP();
            }
            LPC_PUSH(Value::FromI64(0));
            catch_ctx.active = false;
            catch_ctx.end_ip = 0;
        }
#ifdef NDEBUG
        if (LPC_LIKELY(!need_rebind) && LPC_LIKELY(++inner_dispatch_count < kInnerDispatchBatchSize)) {
            goto lpc_inner_dispatch;
        }
#endif
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
                LPC_COMMIT_SP();
                fp = &frames_.back();
                fp->ip = catch_ctx.end_ip;
                if (StackSize() > catch_ctx.stack_watermark) {
                    StackResize(catch_ctx.stack_watermark);
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
#undef LPC_COMMIT_SP
#undef LPC_LOAD_SP
#undef LPC_DISCARD

    EndProfile();
    pending_lifecycle_.clear();
    entry_lifecycle_queue_.clear();
    RuntimeError rebind_err = RebindActiveChunkForCurrentModule();
    if (!rebind_err.ok()) {
        return rebind_err;
    }
    return {};
}
