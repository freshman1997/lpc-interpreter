#include "frontend/verifier.h"

#include <deque>
#include <vector>

namespace lpc {
namespace frontend {

static int EfunResultCount(int efun_index) {
    switch (efun_index) {
    case 0: return 1;  // call_other
    case 1: return 0;  // print
    case 2: return 0;  // puts
    case 3: return 0;  // sleep
    case 4: return 1;  // sizeof
    case 5: return 1;  // random
    case 6: return 1;  // keys
    case 7: return 1;  // values
    case 8: return 1;  // typeof
    case 9: return 1;  // to_string
    case 10: return 1; // to_int
    case 11: return 1; // this_object
    case 12: return 1; // clone_object
    case 13: return 0; // destruct
    case 14: return 1; // sprintf
    case 15: return 0; // write
    case 16: return 1; // time
    case 17: return 1; // member_array
    case 18: return 1; // explode
    case 19: return 1; // implode
    case 20: return 1; // stringp
    case 21: return 1; // intp
    case 22: return 1; // floatp
    case 23: return 1; // arrayp
    case 24: return 1; // mappingp
    case 25: return 1; // objectp
    case 26: return 1; // nullp
    case 27: return 1; // functionp
    case 28: return 1; // to_float
    case 29: return 1; // abs
    case 30: return 1; // strlen
    case 31: return 0; // map_delete
    case 32: return 1; // capitalize
    case 33: return 1; // lower_case
    case 34: return 1; // upper_case
    case 35: return 1; // allocate
    case 36: return 1; // reverse
    case 37: return 1; // min
    case 38: return 1; // max
    case 39: return 1; // sqrt
    case 40: return 1; // ctime
    case 41: return 1; // strsrch
    case 42: return 1; // replace_string
    case 43: return 1; // sort_array
    default: return 0;
    }
}

static bool IsKnownEfun(int efun_index) {
    return efun_index >= 0 && efun_index <= 43;
}

static int StackDelta(MirOp op) {
    switch (op) {
    case MirOp::LoadConst:
    case MirOp::LoadFConst:
    case MirOp::LoadSConst:
    case MirOp::LoadFunction:
    case MirOp::LoadLocal:
    case MirOp::LoadUpvalue:
    case MirOp::LoadGlobal:
        return +1;
    case MirOp::StoreLocal:
    case MirOp::StoreUpvalue:
    case MirOp::StoreGlobal:
        return -1;
    case MirOp::Add:
    case MirOp::Sub:
    case MirOp::Mul:
    case MirOp::Div:
    case MirOp::Mod:
    case MirOp::Shl:
    case MirOp::Shr:
    case MirOp::BitAnd:
    case MirOp::BitOr:
    case MirOp::BitXor:
    case MirOp::LogicAnd:
    case MirOp::LogicOr:
    case MirOp::Eq:
    case MirOp::Neq:
    case MirOp::Lt:
    case MirOp::Lte:
    case MirOp::Gt:
    case MirOp::Gte:
        return -1;
    case MirOp::LogicNot:
    case MirOp::BitNot:
    case MirOp::Neg:
    case MirOp::LoadClassField:
        return 0;
    case MirOp::StoreClassField:
        return -2;
    case MirOp::Index:
        return -1;
    case MirOp::StoreIndex:
        return -2;
    case MirOp::NewClass:
        return +1;
    case MirOp::Call:
    case MirOp::CallEfun:
        return 0;
    case MirOp::ForeachInit:
        return +1;
    case MirOp::ForeachNext:
        return 0;
    case MirOp::JumpIfFalse:
    case MirOp::JumpIfTrue:
        return -1;
    case MirOp::Jump:
    case MirOp::Return:
    case MirOp::Switch:
        return 0;
    case MirOp::Pop:
        return -1;
    case MirOp::Dup:
        return +1;
    case MirOp::NewArray:
    case MirOp::NewMapping:
        return 0;
    case MirOp::Catch:
        return 0;
    default:
        return 0;
    }
}

static Verify2Result VerifyMirFunction(const MirModule &module, const MirFunction &fn, int function_index) {
    Verify2Result r;

    bool has_return = false;
    const int n = static_cast<int>(fn.code.size());
    if (n == 0) {
        return r;
    }

    std::vector<int> in_depth(n, -1);
    std::deque<int> work;
    in_depth[0] = 0;
    work.push_back(0);

    auto fail = [&](const std::string &message, int instr) -> Verify2Result {
        Verify2Result out;
        out.ok = false;
        out.message = message;
        out.function_index = function_index;
        out.instr_index = instr;
        return out;
    };

    auto enqueue = [&](int target, int depth, int from) -> bool {
        if (target < 0 || target > n) {
            r = fail("invalid jump target", from);
            return false;
        }
        if (depth < 0) {
            r = fail("negative stack depth at control-flow edge", from);
            return false;
        }
        if (target == n) {
            return true;
        }
        if (in_depth[target] < 0) {
            in_depth[target] = depth;
            work.push_back(target);
            return true;
        }
        if (in_depth[target] != depth) {
            r = fail("inconsistent stack depth at control-flow join", from);
            return false;
        }
        return true;
    };

    while (!work.empty()) {
        int pc = work.front();
        work.pop_front();
        int depth = in_depth[pc];

        for (int i = pc; i < n; ++i) {
            const MirInstr &ins = fn.code[i];

                if ((ins.op == MirOp::LoadLocal || ins.op == MirOp::StoreLocal) &&
                    (ins.a < 0 || ins.a >= (fn.nargs + static_cast<int>(fn.locals.size())))) {
                    return fail("invalid local slot index", i);
                }

                if ((ins.op == MirOp::LoadUpvalue || ins.op == MirOp::StoreUpvalue) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(fn.upvalues.size()))) {
                    return fail("invalid upvalue slot index", i);
                }

                if ((ins.op == MirOp::LoadGlobal || ins.op == MirOp::StoreGlobal) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.global_variables.size()))) {
                    return fail("invalid global variable index", i);
                }

                if (ins.op == MirOp::LoadFunction &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.functions.size()))) {
                    return fail("invalid function index", i);
                }

                if (ins.op == MirOp::Call && ins.a < 0) {
                    return fail("invalid call argc", i);
                }

                if (ins.op == MirOp::NewClass &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.class_order.size()))) {
                    return fail("invalid class index", i);
                }

                if ((ins.op == MirOp::LoadClassField || ins.op == MirOp::StoreClassField) && ins.a < 0) {
                    return fail("invalid class field index", i);
                }

                if (ins.op == MirOp::Index && ins.a != 0 && ins.a != 1) {
                    return fail("invalid index mode", i);
                }

                if (ins.op == MirOp::ForeachNext && ins.a != 1 && ins.a != 2) {
                    return fail("foreach arity must be 1 or 2", i);
                }

                if (ins.op == MirOp::ForeachNext && (ins.b < 0 || ins.b > n)) {
                    return fail("foreach exit jump target invalid", i);
                }

                if (ins.op == MirOp::Call && depth < ins.a + 1) {
                    return fail("call requires callee and argc values on stack", i);
                }

                if (ins.op == MirOp::CallEfun && (!IsKnownEfun(ins.a) || ins.b < 0)) {
                    return fail("invalid efun call operand", i);
                }

                if (ins.op == MirOp::CallEfun && depth < ins.b) {
                    return fail("efun call requires argc values on stack", i);
                }

                if (ins.op == MirOp::Index && ins.a == 1 && depth < 3) {
                    return fail("range index requires start/container/end on stack", i);
                }

                if (ins.op == MirOp::StoreClassField && depth < 2) {
                    return fail("store class field requires value + class object", i);
                }

                if (ins.op == MirOp::NewArray) {
                    if (ins.a < 0) {
                        return fail("invalid array element count", i);
                    }
                    if (depth < ins.a) {
                        return fail("array literal requires element values on stack", i);
                    }
                }

                if (ins.op == MirOp::LoadConst) {
                    if (ins.a < 0) {
                        return fail("invalid const index", i);
                    }
                    if (fn.iconsts.empty() && ins.a != 0) {
                        return fail("const index out of range", i);
                    }
                    if (!fn.iconsts.empty() && ins.a >= static_cast<int>(fn.iconsts.size())) {
                        return fail("const index out of range", i);
                    }
                }

                if (ins.op == MirOp::LoadSConst) {
                    if (ins.a < 0 || ins.a >= static_cast<int>(fn.sconsts.size())) {
                        return fail("invalid string const index", i);
                    }
                }

                if (ins.op == MirOp::LoadFConst) {
                    if (ins.a < 0 || ins.a >= static_cast<int>(fn.fconsts.size())) {
                        return fail("invalid float const index", i);
                    }
                }

                if ((ins.op == MirOp::LoadFunction) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.functions.size()))) {
                    return fail("invalid function index operand", i);
                }

                if ((ins.op == MirOp::LoadLocal || ins.op == MirOp::StoreLocal) &&
                    (ins.a < 0 || ins.a >= (fn.nargs + static_cast<int>(fn.locals.size())))) {
                    return fail("invalid local slot index operand", i);
                }

                if ((ins.op == MirOp::LoadUpvalue || ins.op == MirOp::StoreUpvalue) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(fn.upvalues.size()))) {
                    return fail("invalid upvalue slot index operand", i);
                }

                if ((ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue) &&
                    (ins.a < 0 || ins.a > n)) {
                    return fail("invalid jump target", i);
                }

                if (ins.op == MirOp::Catch && (ins.a < 0 || ins.a > n)) {
                    return fail("invalid catch end target", i);
                }

                depth += StackDelta(ins.op);

                if (ins.op == MirOp::Call) {
                    depth -= ins.a;
                }

                if (ins.op == MirOp::CallEfun) {
                    depth -= ins.b;
                    depth += EfunResultCount(ins.a);
                }

                if (ins.op == MirOp::Index && ins.a == 1) {
                    depth -= 1;
                }

                if (ins.op == MirOp::NewArray) {
                    depth -= ins.a;
                    depth += 1;
                }

                if (ins.op == MirOp::NewMapping) {
                    if (ins.a < 0) {
                        return fail("invalid mapping pair count", i);
                    }
                    if (depth < 2 * ins.a) {
                        return fail("mapping literal requires key/value values on stack", i);
                    }
                    depth -= 2 * ins.a;
                    depth += 1;
                }
                if (depth < 0) {
                    return fail("stack underflow by MIR instruction", i);
                }

                if (ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue) {
                    if (!enqueue(ins.a, depth, i)) {
                        return r;
                    }
                    if (!enqueue(i + 1, depth, i)) {
                        return r;
                    }
                    break;
                }

                if (ins.op == MirOp::ForeachNext) {
                    if (depth < 2) {
                        return fail("foreach next requires iterator state on stack", i);
                    }
                    if (!enqueue(i + 1, depth + ins.a, i)) {
                        return r;
                    }
                    if (!enqueue(ins.b, depth - 2, i)) {
                        return r;
                    }
                    break;
                }

                if (ins.op == MirOp::Jump) {
                    if (!enqueue(ins.a, depth, i)) {
                        return r;
                    }
                    break;
                }

                if (ins.op == MirOp::Catch) {
                    if (!enqueue(ins.a, depth, i)) {
                        return r;
                    }
                    if (!enqueue(i + 1, depth, i)) {
                        return r;
                    }
                    break;
                }

                if (ins.op == MirOp::Return) {
                    has_return = true;
                    break;
                }

                if (i + 1 < n && in_depth[i + 1] >= 0 && in_depth[i + 1] != depth) {
                    return fail("inconsistent stack depth on fallthrough", i);
                }
                if (i + 1 < n && in_depth[i + 1] < 0) {
                    in_depth[i + 1] = depth;
                }
            }
    }

    if (!has_return) {
        return fail("missing return instruction", n - 1);
    }

    if (in_depth[0] != 0) {
        return fail("invalid entry stack depth", 0);
    }

    // Allow unreachable instructions for now; optimization/cleanup pass can remove them.
    return r;
}

Verify2Result VerifyMirModule(const MirModule &module) {
    for (int fi = 0; fi < static_cast<int>(module.functions.size()); ++fi) {
        Verify2Result r = VerifyMirFunction(module, module.functions[fi], fi);
        if (!r.ok) {
            return r;
        }
    }

    Verify2Result init = VerifyMirFunction(
        module,
        module.init_function,
        static_cast<int>(module.functions.size()));
    if (!init.ok) {
        return init;
    }

    Verify2Result r;
    return r;
}

} // namespace frontend
} // namespace lpc
