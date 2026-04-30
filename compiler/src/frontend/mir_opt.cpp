#include "frontend/mir_opt.h"

#include <cstring>
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

static bool IsUnary(MirOp op) {
    return op == MirOp::Neg || op == MirOp::BitNot || op == MirOp::LogicNot;
}

static MirOp InvertCmp(MirOp op) {
    switch (op) {
    case MirOp::Eq:  return MirOp::Neq;
    case MirOp::Neq: return MirOp::Eq;
    case MirOp::Lt:  return MirOp::Gte;
    case MirOp::Lte: return MirOp::Gt;
    case MirOp::Gt:  return MirOp::Lte;
    case MirOp::Gte: return MirOp::Lt;
    default:         return op;
    }
}

static void BuildTargetMap(const std::vector<MirInstr> &code, std::vector<unsigned char> &is_target) {
    const int n = static_cast<int>(code.size());
    is_target.assign(static_cast<size_t>(n), 0);
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
}

static int AddIConst(MirFunction *f, std::int64_t v) {
    for (int i = 0; i < static_cast<int>(f->iconsts.size()); ++i) {
        if (f->iconsts[i] == v) return i;
    }
    f->iconsts.push_back(v);
    return static_cast<int>(f->iconsts.size()) - 1;
}

static int AddFConst(MirFunction *f, double v) {
    for (int i = 0; i < static_cast<int>(f->fconsts.size()); ++i) {
        if (f->fconsts[i] == v) return i;
    }
    f->fconsts.push_back(v);
    return static_cast<int>(f->fconsts.size()) - 1;
}

static int AddSConst(MirFunction *f, const std::string &v) {
    for (int i = 0; i < static_cast<int>(f->sconsts.size()); ++i) {
        if (f->sconsts[i] == v) return i;
    }
    f->sconsts.push_back(v);
    return static_cast<int>(f->sconsts.size()) - 1;
}

static bool FoldBinary(MirOp op, std::int64_t lhs, std::int64_t rhs, std::int64_t *out) {
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
    case MirOp::BitOr:  *out = lhs | rhs; return true;
    case MirOp::BitXor: *out = lhs ^ rhs; return true;
    case MirOp::Eq:  *out = (lhs == rhs) ? 1 : 0; return true;
    case MirOp::Neq: *out = (lhs != rhs) ? 1 : 0; return true;
    case MirOp::Lt:  *out = (lhs < rhs)  ? 1 : 0; return true;
    case MirOp::Lte: *out = (lhs <= rhs) ? 1 : 0; return true;
    case MirOp::Gt:  *out = (lhs > rhs)  ? 1 : 0; return true;
    case MirOp::Gte: *out = (lhs >= rhs) ? 1 : 0; return true;
    default: return false;
    }
}

static bool FoldFloatBinary(MirOp op, double lhs, double rhs, double *out) {
    if (!out) return false;
    switch (op) {
    case MirOp::Add: *out = lhs + rhs; return true;
    case MirOp::Sub: *out = lhs - rhs; return true;
    case MirOp::Mul: *out = lhs * rhs; return true;
    case MirOp::Div: if (rhs == 0.0) return false; *out = lhs / rhs; return true;
    case MirOp::Eq:  *out = (lhs == rhs) ? 1.0 : 0.0; return true;
    case MirOp::Neq: *out = (lhs != rhs) ? 1.0 : 0.0; return true;
    case MirOp::Lt:  *out = (lhs < rhs)  ? 1.0 : 0.0; return true;
    case MirOp::Lte: *out = (lhs <= rhs) ? 1.0 : 0.0; return true;
    case MirOp::Gt:  *out = (lhs > rhs)  ? 1.0 : 0.0; return true;
    case MirOp::Gte: *out = (lhs >= rhs) ? 1.0 : 0.0; return true;
    default: return false;
    }
}

static bool RewriteLoadConst(MirFunction *f, int pc, std::int64_t value) {
    if (!f || pc < 0 || pc >= static_cast<int>(f->code.size())) return false;
    const int idx = AddIConst(f, value);
    f->code[pc].op = MirOp::LoadConst;
    f->code[pc].a = idx;
    f->code[pc].b = 0;
    return true;
}

static bool IsPowerOfTwo(std::int64_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

static int Log2Int(std::int64_t v) {
    int r = 0;
    while (v > 1) { v >>= 1; ++r; }
    return r;
}

// ─── Pass 1: Local Constant Propagation ────────────────────────────

static bool LocalConstPropagation(MirFunction *f) {
    if (!f || f->code.empty()) return false;
    const int n = static_cast<int>(f->code.size());
    const int nlocals = f->nargs + static_cast<int>(f->locals.size());
    if (nlocals <= 0) return false;

    std::vector<unsigned char> is_target;
    BuildTargetMap(f->code, is_target);

    std::vector<unsigned char> known(static_cast<size_t>(nlocals), 0);
    std::vector<std::int64_t> value(static_cast<size_t>(nlocals), 0);
    auto clear_known = [&]() {
        for (int i = 0; i < nlocals; ++i) known[static_cast<size_t>(i)] = 0;
    };

    bool changed = false;
    for (int i = 0; i < n; ++i) {
        MirInstr &ins = f->code[static_cast<size_t>(i)];
        if (is_target[static_cast<size_t>(i)]) clear_known();

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

// ─── Pass 2: Integer Constant Fold + Algebraic Simplify ────────────

static bool ConstantFoldAndSimplify(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    std::vector<MirInstr> &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    auto skip_nops = [&](int p) {
        int q = p;
        while (q < static_cast<int>(code.size())) {
            if (code[q].op == MirOp::Pop) { ++q; continue; }
            if (code[q].op == MirOp::LoadConst) {
                int ci = code[q].a;
                if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 0) { ++q; continue; }
            }
            break;
        }
        return q;
    };

    for (int i = 2; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 2] || is_target[i - 1] || is_target[i]) continue;
        if (!(IsBinaryArith(code[i].op) || IsBinaryCmp(code[i].op))) continue;
        if (code[i - 2].op != MirOp::LoadConst || code[i - 1].op != MirOp::LoadConst) continue;
        const int ia = code[i - 2].a, ib = code[i - 1].a;
        if (ia < 0 || ib < 0 || ia >= static_cast<int>(f->iconsts.size()) || ib >= static_cast<int>(f->iconsts.size())) continue;
        std::int64_t v = 0;
        if (!FoldBinary(code[i].op, f->iconsts[ia], f->iconsts[ib], &v)) continue;
        RewriteLoadConst(f, i - 2, v);
        const int zidx = AddIConst(f, 0);
        code[i - 1].op = MirOp::LoadConst; code[i - 1].a = zidx; code[i - 1].b = 0;
        code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
        changed = true;
    }

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        const int next_real = skip_nops(i + 1);
        if (next_real < static_cast<int>(code.size())) {
            MirOp nx = code[next_real].op;
            if (nx == MirOp::JumpIfFalse || nx == MirOp::JumpIfTrue || nx == MirOp::ForeachNext || nx == MirOp::Catch) continue;
        }

        if (code[i].op == MirOp::Add && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 0) {
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
            }
        } else if (code[i].op == MirOp::Sub && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 0) {
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
            }
        } else if (code[i].op == MirOp::Mul && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size())) {
                if (f->iconsts[ci] == 1) { code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true; }
            }
        } else if (code[i].op == MirOp::Div && code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci >= 0 && ci < static_cast<int>(f->iconsts.size()) && f->iconsts[ci] == 1) {
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
            }
        }
    }

    return changed;
}

// ─── Pass 3: Float Constant Fold ───────────────────────────────────

static bool FloatConstantFold(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 2; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 2] || is_target[i - 1] || is_target[i]) continue;
        if (!(IsBinaryArith(code[i].op) || IsBinaryCmp(code[i].op))) continue;
        if (code[i - 2].op != MirOp::LoadFConst || code[i - 1].op != MirOp::LoadFConst) continue;
        const int fa = code[i - 2].a, fb = code[i - 1].a;
        if (fa < 0 || fb < 0 || fa >= static_cast<int>(f->fconsts.size()) || fb >= static_cast<int>(f->fconsts.size())) continue;
        double v = 0.0;
        if (!FoldFloatBinary(code[i].op, f->fconsts[fa], f->fconsts[fb], &v)) continue;
        if (IsBinaryCmp(code[i].op)) {
            int idx = AddIConst(f, static_cast<std::int64_t>(v));
            code[i - 2].op = MirOp::LoadConst; code[i - 2].a = idx; code[i - 2].b = 0;
        } else {
            int idx = AddFConst(f, v);
            code[i - 2].op = MirOp::LoadFConst; code[i - 2].a = idx; code[i - 2].b = 0;
        }
        const int zidx = AddIConst(f, 0);
        code[i - 1].op = MirOp::LoadConst; code[i - 1].a = zidx; code[i - 1].b = 0;
        code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
        changed = true;
    }

    return changed;
}

// ─── Pass 4: String Constant Fold (concat + comparison) ────────────

static bool StringConstantFold(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 2; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 2] || is_target[i - 1] || is_target[i]) continue;
        if (code[i - 2].op != MirOp::LoadSConst || code[i - 1].op != MirOp::LoadSConst) continue;
        const int sa = code[i - 2].a, sb = code[i - 1].a;
        if (sa < 0 || sb < 0 || sa >= static_cast<int>(f->sconsts.size()) || sb >= static_cast<int>(f->sconsts.size())) continue;

        if (code[i].op == MirOp::Add) {
            std::string result = f->sconsts[sa] + f->sconsts[sb];
            int idx = AddSConst(f, result);
            code[i - 2].op = MirOp::LoadSConst; code[i - 2].a = idx; code[i - 2].b = 0;
            const int zidx = AddIConst(f, 0);
            code[i - 1].op = MirOp::LoadConst; code[i - 1].a = zidx; code[i - 1].b = 0;
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
            changed = true;
        } else if (code[i].op == MirOp::Eq) {
            int idx = AddIConst(f, f->sconsts[sa] == f->sconsts[sb] ? 1 : 0);
            code[i - 2].op = MirOp::LoadConst; code[i - 2].a = idx; code[i - 2].b = 0;
            const int zidx = AddIConst(f, 0);
            code[i - 1].op = MirOp::LoadConst; code[i - 1].a = zidx; code[i - 1].b = 0;
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
            changed = true;
        } else if (code[i].op == MirOp::Neq) {
            int idx = AddIConst(f, f->sconsts[sa] != f->sconsts[sb] ? 1 : 0);
            code[i - 2].op = MirOp::LoadConst; code[i - 2].a = idx; code[i - 2].b = 0;
            const int zidx = AddIConst(f, 0);
            code[i - 1].op = MirOp::LoadConst; code[i - 1].a = zidx; code[i - 1].b = 0;
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
            changed = true;
        }
    }

    return changed;
}

// ─── Pass 5: Unary Constant Fold (Neg, BitNot, LogicNot) ──────────

static bool UnaryConstantFold(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        const MirOp uop = code[i].op;
        if (!IsUnary(uop)) continue;

        if (code[i - 1].op == MirOp::LoadConst) {
            const int ci = code[i - 1].a;
            if (ci < 0 || ci >= static_cast<int>(f->iconsts.size())) continue;
            std::int64_t v = f->iconsts[ci];
            std::int64_t result = 0;
            if (uop == MirOp::Neg) result = -v;
            else if (uop == MirOp::BitNot) result = ~v;
            else result = (v == 0) ? 1 : 0;
            code[i - 1].a = AddIConst(f, result);
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
            changed = true;
        } else if (code[i - 1].op == MirOp::LoadFConst && uop == MirOp::Neg) {
            const int fi = code[i - 1].a;
            if (fi < 0 || fi >= static_cast<int>(f->fconsts.size())) continue;
            code[i - 1].a = AddFConst(f, -f->fconsts[fi]);
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
            changed = true;
        }
    }

    return changed;
}

// ─── Pass 6: Strength Reduce (mul/bitand/bitor by 0, pow2 mul/div) ─

static bool StrengthReduce(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        if (code[i - 1].op != MirOp::LoadConst) continue;
        const int ci = code[i - 1].a;
        if (ci < 0 || ci >= static_cast<int>(f->iconsts.size())) continue;
        const std::int64_t v = f->iconsts[ci];

        if (code[i].op == MirOp::Mul && v == 0) {
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
        } else if (code[i].op == MirOp::BitAnd && v == 0) {
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
        } else if (code[i].op == MirOp::BitOr && v == 0) {
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
        } else if (code[i].op == MirOp::Mul && IsPowerOfTwo(v) && v != 1) {
            const int shift = Log2Int(v);
            const int sidx = AddIConst(f, shift);
            code[i - 1].a = sidx;
            code[i].op = MirOp::Shl;
            changed = true;
        } else if (code[i].op == MirOp::Div && IsPowerOfTwo(v) && v != 1) {
            const int shift = Log2Int(v);
            const int sidx = AddIConst(f, shift);
            code[i - 1].a = sidx;
            code[i].op = MirOp::Shr;
            changed = true;
        }
    }

    return changed;
}

// ─── Pass 7: Float Algebraic Simplify (add/sub 0.0, mul/div 1.0) ──

static bool FloatAlgebraicSimplify(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        if (code[i - 1].op != MirOp::LoadFConst) continue;
        const int fi = code[i - 1].a;
        if (fi < 0 || fi >= static_cast<int>(f->fconsts.size())) continue;
        const double v = f->fconsts[fi];

        if ((code[i].op == MirOp::Add || code[i].op == MirOp::Sub) && v == 0.0) {
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
        } else if ((code[i].op == MirOp::Mul || code[i].op == MirOp::Div) && v == 1.0) {
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0; changed = true;
        }
    }

    return changed;
}

// ─── Pass 8: Double Negation Elimination ───────────────────────────

static bool DoubleNegationElim(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        MirOp prev = code[i - 1].op;
        MirOp cur = code[i].op;

        if ((prev == MirOp::Neg && cur == MirOp::Neg) ||
            (prev == MirOp::BitNot && cur == MirOp::BitNot) ||
            (prev == MirOp::LogicNot && cur == MirOp::LogicNot)) {
            code[i - 1].op = MirOp::Pop; code[i - 1].a = 0; code[i - 1].b = 0;
            code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
            changed = true;
        }
    }

    return changed;
}

// ─── Pass 9: Compare + LogicNot Merge ──────────────────────────────

static bool CompareLogicNotMerge(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 1; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        if (!IsBinaryCmp(code[i - 1].op)) continue;
        if (code[i].op != MirOp::LogicNot) continue;

        code[i - 1].op = InvertCmp(code[i - 1].op);
        code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
        changed = true;
    }

    return changed;
}

// ─── Pass 10: Self-Comparison Simplify ─────────────────────────────

static bool SelfComparisonSimplify(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 2; i < static_cast<int>(code.size()); ++i) {
        if (is_target[i - 2] || is_target[i - 1] || is_target[i]) continue;
        if (!IsBinaryCmp(code[i].op)) continue;
        if (code[i - 2].op != MirOp::LoadLocal || code[i - 1].op != MirOp::LoadLocal) continue;
        if (code[i - 2].a != code[i - 1].a) continue;

        int result = 0;
        if (code[i].op == MirOp::Eq || code[i].op == MirOp::Lte || code[i].op == MirOp::Gte) {
            result = 1;
        }
        const int ridx = AddIConst(f, result);
        code[i - 2].op = MirOp::LoadConst; code[i - 2].a = ridx; code[i - 2].b = 0;
        const int zidx = AddIConst(f, 0);
        code[i - 1].op = MirOp::LoadConst; code[i - 1].a = zidx; code[i - 1].b = 0;
        code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
        changed = true;
    }

    return changed;
}

// ─── Pass 11: Dead Store Elimination ───────────────────────────────

static bool DeadStoreElim(MirFunction *f) {
    if (!f || f->code.empty()) return false;
    bool changed = false;
    const int n = static_cast<int>(f->code.size());
    const int nlocals = f->nargs + static_cast<int>(f->locals.size());
    if (nlocals <= 0) return false;

    std::vector<unsigned char> is_target;
    BuildTargetMap(f->code, is_target);

    std::vector<unsigned char> kill(static_cast<size_t>(n), 0);

    for (int i = 0; i < n; ++i) {
        if (is_target[i]) continue;
        if (f->code[i].op != MirOp::StoreLocal) continue;
        const int local_idx = f->code[i].a;
        if (local_idx < 0 || local_idx >= nlocals) continue;

        for (int j = i + 1; j < n; ++j) {
            if (is_target[j]) break;
            if (f->code[j].op == MirOp::StoreLocal && f->code[j].a == local_idx) {
                bool read_between = false;
                for (int k = i + 1; k < j; ++k) {
                    if (f->code[k].op == MirOp::LoadLocal && f->code[k].a == local_idx) {
                        read_between = true;
                        break;
                    }
                    if (f->code[k].op == MirOp::StoreLocal && f->code[k].a == local_idx) break;
                }
                if (!read_between) {
                    kill[i] = 1;
                    changed = true;
                }
                break;
            }
            if (f->code[j].op == MirOp::LoadLocal && f->code[j].a == local_idx) break;
            if (f->code[j].op == MirOp::Jump || f->code[j].op == MirOp::JumpIfFalse ||
                f->code[j].op == MirOp::JumpIfTrue || f->code[j].op == MirOp::Return ||
                f->code[j].op == MirOp::ForeachNext || f->code[j].op == MirOp::Catch) break;
        }
    }

    if (!changed) return false;

    int kept = 0;
    for (int i = 0; i < n; ++i) kept += kill[i] ? 0 : 1;
    if (kept == n) return false;

    std::vector<int> next_kept(static_cast<size_t>(n + 1), kept);
    int nxt = kept;
    std::vector<int> old_to_new(static_cast<size_t>(n), -1);
    int cur = 0;
    for (int i = 0; i < n; ++i) {
        if (!kill[i]) { old_to_new[i] = cur++; nxt = old_to_new[i]; }
        next_kept[i] = nxt;
    }
    next_kept[n] = kept;

    std::vector<MirInstr> out;
    out.reserve(static_cast<size_t>(kept));
    for (int i = 0; i < n; ++i) {
        if (!kill[i]) out.push_back(f->code[i]);
    }

    for (MirInstr &ins : out) {
        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) {
            if (ins.a >= 0 && ins.a <= n) ins.a = next_kept[ins.a];
        }
        if (ins.op == MirOp::ForeachNext) {
            if (ins.b >= 0 && ins.b <= n) ins.b = next_kept[ins.b];
        }
    }

    f->code.swap(out);
    return true;
}

// ─── Pass 12: Conditional Jump Merge ───────────────────────────────
//   JumpIfFalse L1; Jump L2; L1:  →  JumpIfTrue L2

static bool ConditionalJumpMerge(MirFunction *f) {
    if (!f || f->code.size() < 3) return false;
    bool changed = false;
    auto &code = f->code;
    const int n = static_cast<int>(code.size());

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 0; i + 2 < n; ++i) {
        if (is_target[i]) continue;
        MirInstr &a = code[i];
        MirInstr &b = code[i + 1];

        if (a.op == MirOp::JumpIfFalse && b.op == MirOp::Jump) {
            int false_target = a.a;
            if (false_target == i + 2 && is_target[i + 2] && is_target[i + 2] == 1) {
                int true_target = b.a;
                a.op = MirOp::JumpIfTrue;
                a.a = true_target;
                b.op = MirOp::Pop; b.a = 0; b.b = 0;
                changed = true;
            }
        } else if (a.op == MirOp::JumpIfTrue && b.op == MirOp::Jump) {
            int true_target = a.a;
            if (true_target == i + 2 && is_target[i + 2] == 1) {
                int false_target = b.a;
                a.op = MirOp::JumpIfFalse;
                a.a = false_target;
                b.op = MirOp::Pop; b.a = 0; b.b = 0;
                changed = true;
            }
        }
    }

    return changed;
}

// ─── Pass 13: Local Common Subexpression Elimination ───────────────

static bool LocalCSE(MirFunction *f) {
    if (!f || f->code.size() < 2) return false;
    bool changed = false;
    auto &code = f->code;
    const int n = static_cast<int>(code.size());

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 1; i < n; ++i) {
        if (is_target[i - 1] || is_target[i]) continue;
        if (code[i - 1].op == MirOp::LoadLocal && code[i].op == MirOp::LoadLocal && code[i - 1].a == code[i].a) {
            code[i].op = MirOp::Dup;
            code[i].a = 0;
            code[i].b = 0;
            changed = true;
        }
    }

    return changed;
}

// ─── Pass 14: Float Const Branch Fold ──────────────────────────────

static bool FloatConstBranchFold(MirFunction *f) {
    if (!f) return false;
    bool changed = false;
    auto &code = f->code;
    const int n = static_cast<int>(code.size());

    std::vector<unsigned char> is_target;
    BuildTargetMap(code, is_target);

    for (int i = 0; i + 1 < n; ++i) {
        if (is_target[i] || is_target[i + 1]) continue;
        if (code[i].op != MirOp::LoadFConst) continue;
        if (code[i + 1].op != MirOp::JumpIfFalse && code[i + 1].op != MirOp::JumpIfTrue) continue;
        const int fi = code[i].a;
        if (fi < 0 || fi >= static_cast<int>(f->fconsts.size())) continue;
        const double v = f->fconsts[fi];
        const bool falsy = (v == 0.0);

        if (code[i + 1].op == MirOp::JumpIfFalse) {
            if (falsy) {
                code[i + 1].op = MirOp::Jump;
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
                changed = true;
            } else {
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
                code[i + 1].op = MirOp::Pop; code[i + 1].a = 0; code[i + 1].b = 0;
                changed = true;
            }
        } else {
            if (!falsy) {
                code[i + 1].op = MirOp::Jump;
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
                changed = true;
            } else {
                code[i].op = MirOp::Pop; code[i].a = 0; code[i].b = 0;
                code[i + 1].op = MirOp::Pop; code[i + 1].a = 0; code[i + 1].b = 0;
                changed = true;
            }
        }
    }

    return changed;
}

// ─── Pass 15: Remove Unreachable Code ──────────────────────────────

static bool RemoveUnreachable(MirFunction *f) {
    if (!f || f->code.empty()) return false;
    const int n = static_cast<int>(f->code.size());
    std::vector<unsigned char> vis(static_cast<size_t>(n), 0);
    std::vector<int> work;
    work.push_back(0);

    auto push_target = [&](int t) {
        if (t >= 0 && t < n && !vis[t]) work.push_back(t);
    };

    while (!work.empty()) {
        const int pc = work.back();
        work.pop_back();
        if (pc < 0 || pc >= n || vis[pc]) continue;
        vis[pc] = 1;
        const MirInstr &ins = f->code[pc];
        switch (ins.op) {
        case MirOp::Jump:          push_target(ins.a); break;
        case MirOp::JumpIfFalse:
        case MirOp::JumpIfTrue:    push_target(ins.a); push_target(pc + 1); break;
        case MirOp::ForeachNext:   push_target(ins.b); push_target(pc + 1); break;
        case MirOp::Catch:         push_target(ins.a); push_target(pc + 1); break;
        case MirOp::Return:        break;
        default:                   push_target(pc + 1); break;
        }
    }

    int kept = 0;
    for (int i = 0; i < n; ++i) kept += vis[i] ? 1 : 0;
    if (kept == n) return false;

    std::vector<int> remap(static_cast<size_t>(n), -1);
    std::vector<MirInstr> out;
    out.reserve(static_cast<size_t>(kept));
    for (int i = 0; i < n; ++i) {
        if (!vis[i]) continue;
        remap[i] = static_cast<int>(out.size());
        out.push_back(f->code[i]);
    }

    for (MirInstr &ins : out) {
        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) {
            if (ins.a >= 0 && ins.a < n && remap[ins.a] >= 0) ins.a = remap[ins.a];
        }
        if (ins.op == MirOp::ForeachNext) {
            if (ins.b >= 0 && ins.b < n && remap[ins.b] >= 0) ins.b = remap[ins.b];
        }
    }

    f->code.swap(out);
    return true;
}

// ─── Pass 16: Peephole + Branch Simplify ───────────────────────────

static bool CompactWithKeepMask(MirFunction *f, const std::vector<unsigned char> &keep) {
    if (!f) return false;
    const int n = static_cast<int>(f->code.size());
    if (static_cast<int>(keep.size()) != n) return false;

    int kept = 0;
    for (int i = 0; i < n; ++i) kept += keep[i] ? 1 : 0;
    if (kept == n) return false;

    std::vector<int> next_kept(static_cast<size_t>(n + 1), kept);
    int nxt = kept;
    std::vector<int> old_to_new(static_cast<size_t>(n), -1);
    int cur = 0;
    for (int i = 0; i < n; ++i) {
        if (keep[i]) { old_to_new[i] = cur++; nxt = old_to_new[i]; }
        next_kept[i] = nxt;
    }
    next_kept[n] = kept;

    std::vector<MirInstr> out;
    out.reserve(static_cast<size_t>(kept));
    for (int i = 0; i < n; ++i) {
        if (keep[i]) out.push_back(f->code[i]);
    }

    for (MirInstr &ins : out) {
        if (ins.op == MirOp::Jump || ins.op == MirOp::JumpIfFalse || ins.op == MirOp::JumpIfTrue || ins.op == MirOp::Catch) {
            if (ins.a >= 0 && ins.a <= n) ins.a = next_kept[ins.a];
        }
        if (ins.op == MirOp::ForeachNext) {
            if (ins.b >= 0 && ins.b <= n) ins.b = next_kept[ins.b];
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
    std::vector<unsigned char> is_target;
    BuildTargetMap(f->code, is_target);

    for (int i = 0; i < n; ++i) {
        MirInstr &ins = f->code[i];
        if (ins.op == MirOp::Jump) {
            int target = ins.a;
            int guard = 0;
            while (target >= 0 && target < n && guard++ < n) {
                const MirInstr &ti = f->code[target];
                if (ti.op != MirOp::Jump || ti.a == target) break;
                target = ti.a;
            }
            if (target != ins.a && target >= 0 && target <= n) { ins.a = target; changed = true; }
            if (ins.a == i + 1) { keep[i] = 0; changed = true; }
        }
    }

    for (int i = 0; i + 1 < n; ++i) {
        MirInstr &a = f->code[i];
        MirInstr &b = f->code[i + 1];

        if (a.op == MirOp::LoadLocal && b.op == MirOp::StoreLocal && a.a == b.a) {
            if (!is_target[i] && !is_target[i + 1]) {
                keep[i] = 0; keep[i + 1] = 0; changed = true; continue;
            }
        }

        if (a.op == MirOp::LoadConst && b.op == MirOp::Pop) {
            if (is_target[i] || is_target[i + 1]) continue;
            bool safe_remove = true;
            if (i + 2 < n) {
                MirOp nx = f->code[i + 2].op;
                if (nx == MirOp::JumpIfFalse || nx == MirOp::JumpIfTrue) safe_remove = false;
            }
            if (!safe_remove) continue;
            keep[i] = 0; keep[i + 1] = 0; changed = true; continue;
        }

        if (a.op == MirOp::LoadConst && (b.op == MirOp::JumpIfFalse || b.op == MirOp::JumpIfTrue)) {
            const int ci = a.a;
            if (ci < 0 || ci >= static_cast<int>(f->iconsts.size())) continue;
            const std::int64_t v = f->iconsts[ci];
            if (b.op == MirOp::JumpIfFalse) {
                if (v == 0) { b.op = MirOp::Jump; keep[i] = 0; changed = true; }
                else        { keep[i] = 0; keep[i + 1] = 0; changed = true; }
            } else {
                if (v != 0) { b.op = MirOp::Jump; keep[i] = 0; changed = true; }
                else        { keep[i] = 0; keep[i + 1] = 0; changed = true; }
            }
        }
    }

    if (!changed) return false;
    CompactWithKeepMask(f, keep);
    return true;
}

// ─── Pipeline ──────────────────────────────────────────────────────

static void OptimizeFunction(MirFunction *f) {
    if (!f) return;

    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;
        changed |= LocalConstPropagation(f);
        changed |= ConstantFoldAndSimplify(f);
        changed |= FloatConstantFold(f);
        changed |= StringConstantFold(f);
        changed |= UnaryConstantFold(f);
        changed |= StrengthReduce(f);
        changed |= FloatAlgebraicSimplify(f);
        changed |= DoubleNegationElim(f);
        changed |= CompareLogicNotMerge(f);
        changed |= SelfComparisonSimplify(f);
        changed |= DeadStoreElim(f);
        changed |= ConditionalJumpMerge(f);
        changed |= FloatConstBranchFold(f);
        if (iter == 0) {
            changed |= LocalCSE(f);
            changed |= PeepholeAndBranchSimplify(f);
        }
        changed |= RemoveUnreachable(f);
        if (!changed) break;
    }

    if (f->code.empty() || f->code.back().op != MirOp::Return) {
        f->code.push_back({MirOp::Return, 0, 0, 0});
    }
}

static void OptimizeFunctionWithStats(MirFunction *f, MirOptStats *st) {
    if (!f) return;
    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;

        auto run = [&](bool (*pass)(MirFunction*), int &cnt, int &delta) {
            const int before = static_cast<int>(f->code.size());
            if (pass(f)) {
                changed = true;
                if (st) { cnt += 1; delta += before - static_cast<int>(f->code.size()); }
            }
        };

        run(LocalConstPropagation,     st->constprop_changed,  st->constprop_instr_delta);
        run(ConstantFoldAndSimplify,    st->constfold_changed,  st->constfold_instr_delta);
        run(FloatConstantFold,          st->constfold_changed,  st->constfold_instr_delta);
        run(StringConstantFold,         st->constfold_changed,  st->constfold_instr_delta);
        run(UnaryConstantFold,          st->constfold_changed,  st->constfold_instr_delta);
        run(StrengthReduce,             st->constfold_changed,  st->constfold_instr_delta);
        run(FloatAlgebraicSimplify,     st->constfold_changed,  st->constfold_instr_delta);
        run(DoubleNegationElim,         st->constfold_changed,  st->constfold_instr_delta);
        run(CompareLogicNotMerge,       st->constfold_changed,  st->constfold_instr_delta);
        run(SelfComparisonSimplify,     st->constfold_changed,  st->constfold_instr_delta);
        run(DeadStoreElim,              st->constfold_changed,  st->constfold_instr_delta);
        run(ConditionalJumpMerge,       st->constfold_changed,  st->constfold_instr_delta);
        run(FloatConstBranchFold,       st->constfold_changed,  st->constfold_instr_delta);

        if (iter == 0) {
            run(LocalCSE, st->cse_changed, st->cse_instr_delta);
            run(PeepholeAndBranchSimplify, st->peephole_changed, st->peephole_instr_delta);
        }

        run(RemoveUnreachable, st->unreachable_changed, st->unreachable_instr_delta);

        if (!changed) break;
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
    if (!module) return st;

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
