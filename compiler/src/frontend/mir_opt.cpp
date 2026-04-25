#include "frontend/mir_opt.h"

#include <vector>

namespace lpc {
namespace frontend {

namespace {

static bool IsBinaryArith(MirOp op) {
    return op == MirOp::Add || op == MirOp::Sub || op == MirOp::Mul || op == MirOp::Div || op == MirOp::Mod ||
        op == MirOp::Shl || op == MirOp::Shr || op == MirOp::BitAnd || op == MirOp::BitOr || op == MirOp::BitXor;
}

static bool IsBinaryCmp(MirOp op) {
    return op == MirOp::Eq || op == MirOp::Neq || op == MirOp::Lt || op == MirOp::Lte || op == MirOp::Gt || op == MirOp::Gte;
}

static int AddIConst(MirFunction *f, int v) {
    for (int i = 0; i < static_cast<int>(f->iconsts.size()); ++i) {
        if (f->iconsts[i] == v) {
            return i;
        }
    }
    f->iconsts.push_back(v);
    return static_cast<int>(f->iconsts.size()) - 1;
}

static bool FoldBinary(MirOp op, int lhs, int rhs, int *out) {
    if (!out) return false;
    switch (op) {
    case MirOp::Add: *out = lhs + rhs; return true;
    case MirOp::Sub: *out = lhs - rhs; return true;
    case MirOp::Mul: *out = lhs * rhs; return true;
    case MirOp::Div: if (rhs == 0) return false; *out = lhs / rhs; return true;
    case MirOp::Mod: if (rhs == 0) return false; *out = lhs % rhs; return true;
    case MirOp::Shl: *out = lhs << rhs; return true;
    case MirOp::Shr: *out = lhs >> rhs; return true;
    case MirOp::BitAnd: *out = lhs & rhs; return true;
    case MirOp::BitOr: *out = lhs | rhs; return true;
    case MirOp::BitXor: *out = lhs ^ rhs; return true;
    case MirOp::Eq: *out = (lhs == rhs) ? 1 : 0; return true;
    case MirOp::Neq: *out = (lhs != rhs) ? 1 : 0; return true;
    case MirOp::Lt: *out = (lhs < rhs) ? 1 : 0; return true;
    case MirOp::Lte: *out = (lhs <= rhs) ? 1 : 0; return true;
    case MirOp::Gt: *out = (lhs > rhs) ? 1 : 0; return true;
    case MirOp::Gte: *out = (lhs >= rhs) ? 1 : 0; return true;
    default:
        return false;
    }
}

static bool RewriteLoadConst(MirFunction *f, int pc, int value) {
    if (!f || pc < 0 || pc >= static_cast<int>(f->code.size())) return false;
    const int idx = AddIConst(f, value);
    f->code[pc].op = MirOp::LoadConst;
    f->code[pc].a = idx;
    f->code[pc].b = 0;
    return true;
}

static bool ConstantFoldAndSimplify(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    std::vector<MirInstr> &code = f->code;
    const int n = static_cast<int>(code.size());

    std::vector<unsigned char> is_target(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const MirInstr &ins = code[static_cast<size_t>(i)];
        if ((ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) &&
            ins.a >= 0 && ins.a < n) {
            is_target[static_cast<size_t>(ins.a)] = 1;
        }
        if (ins.op == MirOp::ForeachNext && ins.b >= 0 && ins.b < n) {
            is_target[static_cast<size_t>(ins.b)] = 1;
        }
    }

    auto skip_nops = [&](int p) {
        int q = p;
        while (q < static_cast<int>(code.size())) {
            if (code[q].op == MirOp::Pop) {
                ++q;
                continue;
            }
            if (code[q].op == MirOp::LoadConst) {
                int ci = code[q].a;
                if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 0) {
                    ++q;
                    continue;
                }
            }
            break;
        }
        return q;
    };

    for (int i = 2; i < static_cast<int>(code.size()); ++i) {
        if (is_target[static_cast<size_t>(i - 2)] || is_target[static_cast<size_t>(i - 1)] || is_target[static_cast<size_t>(i)]) {
            continue;
        }
        if (!(IsBinaryArith(code[i].op) || IsBinaryCmp(code[i].op))) {
            continue;
        }
        if (code[i - 2].op != MirOp::LoadConst || code[i - 1].op != MirOp::LoadConst) {
            continue;
        }
        const int ia = code[i - 2].a;
        const int ib = code[i - 1].a;
        if (ia < 0 || ib < 0 || ia >= static_cast<int>(f->iconsts.size()) || ib >= static_cast<int>(f->iconsts.size())) {
            continue;
        }
        int v = 0;
        if (!FoldBinary(code[i].op, f->iconsts[ia], f->iconsts[ib], &v)) {
            continue;
        }
        RewriteLoadConst(f, i - 2, v);
        const int zidx = AddIConst(f, 0);
        code[i - 1].op = MirOp::LoadConst;
        code[i - 1].a = zidx;
        code[i - 1].b = 0;
        code[i].op = MirOp::Pop;
        code[i].a = 0;
        code[i].b = 0;
        changed = true;
    }

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[static_cast<size_t>(i - 1)] || is_target[static_cast<size_t>(i)]) {
            continue;
        }
        const int next_real = skip_nops(i + 1);
        if (next_real < static_cast<int>(code.size())) {
            MirOp nx = code[next_real].op;
            if (nx == MirOp::JumpIfFalse || nx == MirOp::JumpIfTrue || nx == MirOp::ForeachNext || nx == MirOp::Catch) {
                continue;
            }
        }

        if (code[i].op == MirOp::Add && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 0) {
                code[i].op = MirOp::Pop;
                code[i].a = 0;
                code[i].b = 0;
                changed = true;
            }
        } else if (code[i].op == MirOp::Sub && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 0) {
                code[i].op = MirOp::Pop;
                code[i].a = 0;
                code[i].b = 0;
                changed = true;
            }
        } else if (code[i].op == MirOp::Mul && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size())) {
                if (f->iconsts[ci] == 1) {
                    code[i].op = MirOp::Pop;
                    code[i].a = 0;
                    code[i].b = 0;
                    changed = true;
                }
            }
        } else if (code[i].op == MirOp::Div && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 1) {
                code[i].op = MirOp::Pop;
                code[i].a = 0;
                code[i].b = 0;
                changed = true;
            }
        }
    }

    return changed;
}

static bool RemoveUnreachable(MirFunction *f) {
    if (!f || f->code.empty()) return false;
    const int n = static_cast<int>(f->code.size());
    std::vector<unsigned char> vis(static_cast<size_t>(n), 0);
    std::vector<int> work;
    work.push_back(0);

    auto push_target = [&](int t) {
        if (t >= 0 && t < n && !vis[static_cast<size_t>(t)]) {
            work.push_back(t);
        }
    };

    while (!work.empty()) {
        const int pc = work.back();
        work.pop_back();
        if (pc < 0 || pc >= n || vis[static_cast<size_t>(pc)]) {
            continue;
        }
        vis[static_cast<size_t>(pc)] = 1;
        const MirInstr &ins = f->code[pc];

        switch (ins.op) {
        case MirOp::Jump:
            push_target(ins.a);
            break;
        case MirOp::JumpIfFalse:
        case MirOp::JumpIfTrue:
            push_target(ins.a);
            push_target(pc + 1);
            break;
        case MirOp::ForeachNext:
            push_target(ins.b);
            push_target(pc + 1);
            break;
        case MirOp::Catch:
            push_target(ins.a);
            push_target(pc + 1);
            break;
        case MirOp::Return:
            break;
        default:
            push_target(pc + 1);
            break;
        }
    }

    int kept = 0;
    for (int i = 0; i < n; ++i) kept += vis[static_cast<size_t>(i)] ? 1 : 0;
    if (kept == n) return false;

    std::vector<int> remap(static_cast<size_t>(n), -1);
    std::vector<MirInstr> out;
    out.reserve(static_cast<size_t>(kept));
    for (int i = 0; i < n; ++i) {
        if (!vis[static_cast<size_t>(i)]) continue;
        remap[static_cast<size_t>(i)] = static_cast<int>(out.size());
        out.push_back(f->code[static_cast<size_t>(i)]);
    }

    for (MirInstr &ins : out) {
        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) {
            if (ins.a >= 0 && ins.a < n && remap[static_cast<size_t>(ins.a)] >= 0) {
                ins.a = remap[static_cast<size_t>(ins.a)];
            }
        }
        if (ins.op == MirOp::ForeachNext) {
            if (ins.b >= 0 && ins.b < n && remap[static_cast<size_t>(ins.b)] >= 0) {
                ins.b = remap[static_cast<size_t>(ins.b)];
            }
        }
    }

    f->code.swap(out);
    return true;
}

static bool CompactWithKeepMask(MirFunction *f, const std::vector<unsigned char> &keep) {
    if (!f) return false;
    const int n = static_cast<int>(f->code.size());
    if (static_cast<int>(keep.size()) != n) return false;

    int kept = 0;
    for (int i = 0; i < n; ++i) kept += keep[static_cast<size_t>(i)] ? 1 : 0;
    if (kept == n) return false;

    std::vector<int> next_kept(static_cast<size_t>(n + 1), kept);
    int next = kept;
    std::vector<int> old_to_new(static_cast<size_t>(n), -1);
    int cur = 0;
    for (int i = 0; i < n; ++i) {
        if (keep[static_cast<size_t>(i)]) {
            old_to_new[static_cast<size_t>(i)] = cur++;
            next = old_to_new[static_cast<size_t>(i)];
        }
        next_kept[static_cast<size_t>(i)] = next;
    }
    next_kept[static_cast<size_t>(n)] = kept;

    std::vector<MirInstr> out;
    out.reserve(static_cast<size_t>(kept));
    for (int i = 0; i < n; ++i) {
        if (keep[static_cast<size_t>(i)]) {
            out.push_back(f->code[static_cast<size_t>(i)]);
        }
    }

    for (MirInstr &ins : out) {
        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) {
            if (ins.a >= 0 && ins.a <= n) {
                ins.a = next_kept[static_cast<size_t>(ins.a)];
            }
        }
        if (ins.op == MirOp::ForeachNext) {
            if (ins.b >= 0 && ins.b <= n) {
                ins.b = next_kept[static_cast<size_t>(ins.b)];
            }
        }
    }

    f->code.swap(out);
    return true;
}

static bool PeepholeAndBranchSimplify(MirFunction *f) {
    if (!f || f->code.empty()) return false;
    bool changed = false;
    const int n = static_cast<int>(f->code.size());
    std::vector<unsigned char> keep(static_cast<size_t>(n), 1);
    std::vector<unsigned char> is_target(static_cast<size_t>(n), 0);

    for (int i = 0; i < n; ++i) {
        const MirInstr &ins = f->code[static_cast<size_t>(i)];
        if ((ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) &&
            ins.a >= 0 && ins.a < n) {
            is_target[static_cast<size_t>(ins.a)] = 1;
        }
        if (ins.op == MirOp::ForeachNext && ins.b >= 0 && ins.b < n) {
            is_target[static_cast<size_t>(ins.b)] = 1;
        }
    }

    for (int i = 0; i < n; ++i) {
        MirInstr &ins = f->code[static_cast<size_t>(i)];
        if (ins.op == MirOp::Jump) {
            int target = ins.a;
            int guard = 0;
            while (target >= 0 && target < n && guard++ < n) {
                const MirInstr &ti = f->code[static_cast<size_t>(target)];
                if (ti.op != MirOp::Jump || ti.a == target) {
                    break;
                }
                target = ti.a;
            }
            if (target != ins.a && target >= 0 && target <= n) {
                ins.a = target;
                changed = true;
            }
            if (ins.a == i + 1) {
                keep[static_cast<size_t>(i)] = 0;
                changed = true;
            }
        }
    }

    for (int i = 0; i + 1 < n; ++i) {
        MirInstr &a = f->code[static_cast<size_t>(i)];
        MirInstr &b = f->code[static_cast<size_t>(i + 1)];

        if (a.op == MirOp::LoadLocal && b.op == MirOp::StoreLocal && a.a == b.a) {
            if (!is_target[static_cast<size_t>(i)] && !is_target[static_cast<size_t>(i + 1)]) {
                keep[static_cast<size_t>(i)] = 0;
                keep[static_cast<size_t>(i + 1)] = 0;
                changed = true;
                continue;
            }
        }

        if (a.op == MirOp::LoadConst && b.op == MirOp::Pop) {
            if (is_target[static_cast<size_t>(i)] || is_target[static_cast<size_t>(i + 1)]) {
                continue;
            }
            bool safe_remove = true;
            if (i + 2 < n) {
                MirOp nx = f->code[static_cast<size_t>(i + 2)].op;
                if (nx == MirOp::JumpIfFalse || nx == MirOp::JumpIfTrue) {
                    safe_remove = false;
                }
            }
            if (!safe_remove) {
                continue;
            }
            keep[static_cast<size_t>(i)] = 0;
            keep[static_cast<size_t>(i + 1)] = 0;
            changed = true;
            continue;
        }

        if (a.op == MirOp::LoadConst && (b.op == MirOp::JumpIfFalse || b.op == MirOp::JumpIfTrue)) {
            const int ci = a.a;
            if (ci < 0 || ci >= static_cast<int>(f->iconsts.size())) {
                continue;
            }
            const int v = f->iconsts[static_cast<size_t>(ci)];
            if (b.op == MirOp::JumpIfFalse) {
                if (v == 0) {
                    b.op = MirOp::Jump;
                    keep[static_cast<size_t>(i)] = 0;
                    changed = true;
                } else {
                    keep[static_cast<size_t>(i)] = 0;
                    keep[static_cast<size_t>(i + 1)] = 0;
                    changed = true;
                }
            } else {
                if (v != 0) {
                    b.op = MirOp::Jump;
                    keep[static_cast<size_t>(i)] = 0;
                    changed = true;
                } else {
                    keep[static_cast<size_t>(i)] = 0;
                    keep[static_cast<size_t>(i + 1)] = 0;
                    changed = true;
                }
            }
        }
    }

    if (!changed) {
        return false;
    }
    CompactWithKeepMask(f, keep);
    return true;
}

static bool LocalConstPropagation(MirFunction *f) {
    if (!f || f->code.empty()) return false;
    const int n = static_cast<int>(f->code.size());
    const int nlocals = f->nargs + static_cast<int>(f->locals.size());
    if (nlocals <= 0) return false;

    std::vector<unsigned char> is_target(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const MirInstr &ins = f->code[static_cast<size_t>(i)];
        if ((ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) &&
            ins.a >= 0 && ins.a < n) {
            is_target[static_cast<size_t>(ins.a)] = 1;
        }
        if (ins.op == MirOp::ForeachNext && ins.b >= 0 && ins.b < n) {
            is_target[static_cast<size_t>(ins.b)] = 1;
        }
    }

    std::vector<unsigned char> known(static_cast<size_t>(nlocals), 0);
    std::vector<int> value(static_cast<size_t>(nlocals), 0);
    auto clear_known = [&]() {
        for (int i = 0; i < nlocals; ++i) {
            known[static_cast<size_t>(i)] = 0;
        }
    };

    bool changed = false;
    for (int i = 0; i < n; ++i) {
        MirInstr &ins = f->code[static_cast<size_t>(i)];
        if (is_target[static_cast<size_t>(i)]) {
            clear_known();
        }

        if (ins.op == MirOp::LoadLocal && ins.a >= 0 && ins.a < nlocals && known[static_cast<size_t>(ins.a)]) {
            ins.op = MirOp::LoadConst;
            ins.a = AddIConst(f, value[static_cast<size_t>(ins.a)]);
            ins.b = 0;
            changed = true;
        }

        if (ins.op == MirOp::StoreLocal && ins.a >= 0 && ins.a < nlocals) {
            const bool prev_const = (i > 0 && !is_target[static_cast<size_t>(i)] &&
                f->code[static_cast<size_t>(i - 1)].op == MirOp::LoadConst);
            if (prev_const) {
                int ci = f->code[static_cast<size_t>(i - 1)].a;
                if (ci >= 0 && ci < static_cast<int>(f->iconsts.size())) {
                    known[static_cast<size_t>(ins.a)] = 1;
                    value[static_cast<size_t>(ins.a)] = f->iconsts[static_cast<size_t>(ci)];
                } else {
                    known[static_cast<size_t>(ins.a)] = 0;
                }
            } else {
                known[static_cast<size_t>(ins.a)] = 0;
            }
        }

        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue ||
            ins.op == MirOp::Return || ins.op == MirOp::ForeachNext || ins.op == MirOp::Catch) {
            clear_known();
        }
    }
    return changed;
}

static void OptimizeFunction(MirFunction *f) {
    if (!f) return;

    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;
        changed |= LocalConstPropagation(f);
        changed |= ConstantFoldAndSimplify(f);
        if (iter == 0) {
            changed |= PeepholeAndBranchSimplify(f);
        }
        changed |= RemoveUnreachable(f);
        if (!changed) {
            break;
        }
    }

    if (f->code.empty() || f->code.back().op != MirOp::Return) {
        f->code.push_back({MirOp::Return, 0, 0, 0});
    }
}

static void OptimizeFunctionWithStats(MirFunction *f, MirOptStats *st) {
    if (!f) return;
    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;

        const int before_cp = static_cast<int>(f->code.size());
        if (LocalConstPropagation(f)) {
            changed = true;
            if (st) {
                st->constprop_changed += 1;
                st->constprop_instr_delta += before_cp - static_cast<int>(f->code.size());
            }
        }

        const int before_cf = static_cast<int>(f->code.size());
        if (ConstantFoldAndSimplify(f)) {
            changed = true;
            if (st) {
                st->constfold_changed += 1;
                st->constfold_instr_delta += before_cf - static_cast<int>(f->code.size());
            }
        }

        if (iter == 0) {
            const int before_ph = static_cast<int>(f->code.size());
            if (PeepholeAndBranchSimplify(f)) {
                changed = true;
                if (st) {
                    st->peephole_changed += 1;
                    st->peephole_instr_delta += before_ph - static_cast<int>(f->code.size());
                }
            }
        }

        const int before_ur = static_cast<int>(f->code.size());
        if (RemoveUnreachable(f)) {
            changed = true;
            if (st) {
                st->unreachable_changed += 1;
                st->unreachable_instr_delta += before_ur - static_cast<int>(f->code.size());
            }
        }

        if (!changed) {
            break;
        }
    }

    if (f->code.empty() || f->code.back().op != MirOp::Return) {
        f->code.push_back({MirOp::Return, 0, 0, 0});
    }
}

} // namespace

void OptimizeMirModule(MirModule *module) {
    if (!module) return;
    for (MirFunction &fn : module->functions) {
        OptimizeFunction(&fn);
    }
    OptimizeFunction(&module->init_function);
}

MirOptStats OptimizeMirModuleWithStats(MirModule *module) {
    MirOptStats st;
    if (!module) {
        return st;
    }

    st.function_count = static_cast<int>(module->functions.size());
    st.init_count = static_cast<int>(module->init_function.code.size());
    for (const MirFunction &fn : module->functions) {
        st.before_instr += static_cast<int>(fn.code.size());
    }
    st.before_instr += static_cast<int>(module->init_function.code.size());

    for (MirFunction &fn : module->functions) {
        OptimizeFunctionWithStats(&fn, &st);
    }
    OptimizeFunctionWithStats(&module->init_function, &st);

    for (const MirFunction &fn : module->functions) {
        st.after_instr += static_cast<int>(fn.code.size());
    }
    st.after_instr += static_cast<int>(module->init_function.code.size());
    return st;
}

} // namespace frontend
} // namespace lpc
