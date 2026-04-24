#include "frontend2/verifier2.h"

#include <deque>
#include <vector>

namespace lpc {
namespace frontend2 {

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
        return 0;    case MirOp::ForeachInit:
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
    case MirOp::NewMapping:
        return 0;
    case MirOp::Catch:
        return 0;
    default:
        return 0;
    }
}

Verify2Result VerifyMirModule(const MirModule &module) {
    Verify2Result r;

    for (int fi = 0; fi < static_cast<int>(module.functions.size()); ++fi) {
        const MirFunction &fn = module.functions[fi];
        bool has_return = false;
        const int n = static_cast<int>(fn.code.size());
        if (n == 0) {
            continue;
        }

        std::vector<int> in_depth(n, -1);
        std::deque<int> work;
        in_depth[0] = 0;
        work.push_back(0);

        auto enqueue = [&](int target, int depth, int from) -> bool {
            if (target < 0 || target > n) {
                r.ok = false;
                r.message = "invalid jump target";
                r.function_index = fi;
                r.instr_index = from;
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
                r.ok = false;
                r.message = "inconsistent stack depth at control-flow join";
                r.function_index = fi;
                r.instr_index = from;
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
                    r.ok = false;
                    r.message = "invalid local slot index";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if ((ins.op == MirOp::LoadUpvalue || ins.op == MirOp::StoreUpvalue) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(fn.upvalues.size()))) {
                    r.ok = false;
                    r.message = "invalid upvalue slot index";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if ((ins.op == MirOp::LoadGlobal || ins.op == MirOp::StoreGlobal) &&
                    (ins.a < 0)) {
                    r.ok = false;
                    r.message = "invalid global variable index";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::LoadFunction &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.functions.size()))) {
                    r.ok = false;
                    r.message = "invalid function index";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::Call && ins.a < 0) {
                    r.ok = false;
                    r.message = "invalid call argc";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::NewClass &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.class_order.size()))) {
                    r.ok = false;
                    r.message = "invalid class index";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if ((ins.op == MirOp::LoadClassField || ins.op == MirOp::StoreClassField) && ins.a < 0) {
                    r.ok = false;
                    r.message = "invalid class field index";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::Index && ins.a != 0 && ins.a != 1) {
                    r.ok = false;
                    r.message = "invalid index mode";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::ForeachNext && ins.a != 1 && ins.a != 2) {
                    r.ok = false;
                    r.message = "foreach arity must be 1 or 2";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::ForeachNext && (ins.b < 0 || ins.b > n)) {
                    r.ok = false;
                    r.message = "foreach exit jump target invalid";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::Call && depth < 1) {
                    r.ok = false;
                    r.message = "call requires callee + argc on stack";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::CallEfun && ins.b < 0) {
                    r.ok = false;
                    r.message = "invalid efun argc";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::Index && ins.a == 1 && depth < 3) {
                    r.ok = false;
                    r.message = "range index requires start/container/end on stack";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::StoreClassField && depth < 2) {
                    r.ok = false;
                    r.message = "store class field requires value + class object";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::LoadConst) {
                    if (ins.a < 0) {
                        r.ok = false;
                        r.message = "invalid const index";
                        r.function_index = fi;
                        r.instr_index = i;
                        return r;
                    }
                    if (fn.iconsts.empty() && ins.a != 0) {
                        r.ok = false;
                        r.message = "const index out of range";
                        r.function_index = fi;
                        r.instr_index = i;
                        return r;
                    }
                    if (!fn.iconsts.empty() && ins.a >= static_cast<int>(fn.iconsts.size())) {
                        r.ok = false;
                        r.message = "const index out of range";
                        r.function_index = fi;
                        r.instr_index = i;
                        return r;
                    }
                }

                if (ins.op == MirOp::LoadSConst) {
                    if (ins.a < 0 || ins.a >= static_cast<int>(fn.sconsts.size())) {
                        r.ok = false;
                        r.message = "invalid string const index";
                        r.function_index = fi;
                        r.instr_index = i;
                        return r;
                    }
                }

                if (ins.op == MirOp::LoadFConst) {
                    if (ins.a < 0 || ins.a >= static_cast<int>(fn.fconsts.size())) {
                        r.ok = false;
                        r.message = "invalid float const index";
                        r.function_index = fi;
                        r.instr_index = i;
                        return r;
                    }
                }

                if ((ins.op == MirOp::LoadFunction) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(module.functions.size()))) {
                    r.ok = false;
                    r.message = "invalid function index operand";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if ((ins.op == MirOp::LoadLocal || ins.op == MirOp::StoreLocal) &&
                    (ins.a < 0 || ins.a >= (fn.nargs + static_cast<int>(fn.locals.size())))) {
                    r.ok = false;
                    r.message = "invalid local slot index operand";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if ((ins.op == MirOp::LoadUpvalue || ins.op == MirOp::StoreUpvalue) &&
                    (ins.a < 0 || ins.a >= static_cast<int>(fn.upvalues.size()))) {
                    r.ok = false;
                    r.message = "invalid upvalue slot index operand";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if ((ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue) &&
                    (ins.a < 0 || ins.a > n)) {
                    r.ok = false;
                    r.message = "invalid jump target";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }

                if (ins.op == MirOp::Catch && (ins.a < 0 || ins.a > n)) {
                    r.ok = false;
                    r.message = "invalid catch end target";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
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

                if (ins.op == MirOp::NewMapping) {
                    if (ins.a < 0) {
                        r.ok = false;
                        r.message = "invalid mapping pair count";
                        r.function_index = fi;
                        r.instr_index = i;
                        return r;
                    }
                    depth -= 2 * ins.a;
                    depth += 1;
                }
                if (depth < 0) {
                    r.ok = false;
                    r.message = "stack underflow by MIR instruction";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
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
                    r.ok = false;
                    r.message = "inconsistent stack depth on fallthrough";
                    r.function_index = fi;
                    r.instr_index = i;
                    return r;
                }
                if (i + 1 < n && in_depth[i + 1] < 0) {
                    in_depth[i + 1] = depth;
                }
            }
        }

        if (!has_return) {
            r.ok = false;
            r.message = "missing return instruction";
            r.function_index = fi;
            r.instr_index = n - 1;
            return r;
        }

        if (in_depth[0] != 0) {
            r.ok = false;
            r.message = "invalid entry stack depth";
            r.function_index = fi;
            r.instr_index = 0;
            return r;
        }

        // Allow unreachable instructions for now; optimization/cleanup pass can remove them.
    }

    return r;
}

} // namespace frontend2
} // namespace lpc
